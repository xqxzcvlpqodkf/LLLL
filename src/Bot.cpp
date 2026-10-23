#include "Bot.hpp"

#include "Console.hpp"
#include "Heuristic.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;

namespace {

// Reads the gate-relevant fields off a level. All of these are plain
// GJGameLevel fields, no guessing involved.
learnbot::LevelInfo describeLevel(GJGameLevel* level) {
    learnbot::LevelInfo info;
    if (!level) {
        return info;
    }
    info.demon = static_cast<int>(level->m_demon);
    info.demonDifficulty = level->m_demonDifficulty;
    info.stars = static_cast<int>(level->m_stars);
    info.normalPercent = static_cast<int>(level->m_normalPercent);
    return info;
}

// "Effort" is a 1-10 knob that turns into a simulation budget. The numbers are
// in ticks (240 per second of gameplay); the top end is roughly ten minutes of
// thinking on a normal machine.
long long budgetForEffort(int effort) {
    static constexpr long long kTable[] = {
        0,
        500'000,      // 1
        1'500'000,    // 2
        3'000'000,    // 3
        6'000'000,    // 4
        12'000'000,   // 5
        25'000'000,   // 6  (default)
        50'000'000,   // 7
        100'000'000,  // 8
        180'000'000,  // 9
        300'000'000,  // 10 - about ten minutes
    };
    int const index = std::clamp(effort, 1, 10);
    return kTable[index];
}

learnbot::GateSettings gateFromSettings() {
    learnbot::GateSettings gate;
    gate.blockEasyDemon = g_bot.blockEasyDemon;
    gate.blockHardDemon = g_bot.blockHardDemon;
    gate.blockMediumDemon = g_bot.blockMediumDemon;
    gate.blockInsaneDemon = g_bot.blockInsaneDemon;
    gate.blockExtremeDemon = g_bot.blockExtremeDemon;
    gate.blockCompletedLevels = g_bot.blockCompletedLevels;
    return gate;
}

} // namespace

LearnBot& LearnBot::get() {
    static LearnBot instance;
    return instance;
}

// -- lifecycle ------------------------------------------------------------

void LearnBot::onLevelStart(PlayLayer* layer) {
    m_layer = layer;
    m_activeSim = nullptr;
    m_mode = Mode::Off;
    m_replayTick = 0;
    m_best = 0.0;
    m_plan.clear();
    m_blockedReason.clear();
    m_learnedOnce = false;
    m_warmupFrames = 0;
    m_lastPlayerX = -1e9f;

    // GJBaseGameLayer::m_isEditor is true for LevelEditorLayer. In 2.2 both
    // levels and the editor derive from GJBaseGameLayer, and this is the
    // cheapest reliable way to tell them apart without RTTI.
    if (!layer || layer->m_isEditor) {
        return;
    }

    if (!g_bot.enabled) {
        return;
    }

    dbg::section("Level check");
    auto const levelName = layer->m_level
        ? std::string(layer->m_level->m_levelName)
        : std::string("(unknown)");
    dbg::say(fmt::format("Level: \"{}\"", levelName));

    auto const gate = learnbot::evaluate(describeLevel(layer->m_level), gateFromSettings());
    if (!gate.allowed) {
        m_blockedReason = learnbot::reasonText(gate);
        m_mode = Mode::Blocked;
        dbg::say(fmt::format("Not running here: this level is a {}.", m_blockedReason), dbg::Tone::Bad);
        dbg::say("That is one of the difficulties LearnBot is set never to touch.", dbg::Tone::Bad);
        log::info("LearnBot: refusing to run on this level ({})", m_blockedReason);
        return;
    }
    dbg::say("Allowed. I will start thinking as soon as the level starts.", dbg::Tone::Good);

    loadRoute(layer->m_level);
    m_mode = Mode::Waiting;
    if (!m_plan.empty()) {
        dbg::say(fmt::format("I already know {} steps of this level from last time.",
            m_plan.size()));
    }
    log::info("LearnBot: armed ({} steps of saved route)", m_plan.size());
}

bool LearnBot::beforeUpdate(GJBaseGameLayer* layer, GameSim::Advance advance) {
    if (!m_layer || layer != static_cast<GJBaseGameLayer*>(m_layer)) {
        return false;
    }

    if (m_mode == Mode::Waiting) {
        // Wait until the level is genuinely simulating before taking the loop
        // over. The first frames after PlayLayer::init have no player and no
        // running game state; hijacking them means our manual update() calls
        // do nothing at all, which is indistinguishable from "the bot did
        // nothing for two minutes".
        auto player = m_layer->m_player1;
        if (!player) {
            ++m_warmupFrames;
            return false;  // let the game build itself
        }

        float const x = player->getPositionX();
        bool const moving = std::abs(x - m_lastPlayerX) > 0.01f;
        m_lastPlayerX = x;

        if (!moving && m_warmupFrames < 600) {
            ++m_warmupFrames;
            return false;  // player exists but the level has not started moving
        }

        startLearning(m_layer, std::move(advance));
        return true;  // the learning pass consumed this frame
    }
    if (m_mode == Mode::Learning) {
        // A learning pass never spans frames, but if that ever changes this
        // keeps the game loop owned by the bot until it is done.
        return true;
    }

    pushLiveInput(m_layer);
    return false;
}

void LearnBot::afterUpdate(GJBaseGameLayer* /*layer*/) {
    // Nothing to do: live input has to be pushed before the original update.
}

bool LearnBot::onDeath(PlayerObject* /*player*/, GameObject* /*object*/) {
    if (m_mode == Mode::Learning && m_activeSim) {
        // The learning pass observes the hit instead of dying to it. This is
        // what makes the search possible at all, so it always applies here.
        m_activeSim->markDied();
        return true;
    }
    if (m_mode == Mode::Replay && g_bot.noclipFallback) {
        return true;
    }
    return false;
}

void LearnBot::onReset() {
    m_replayTick = 0;

    // The learning pass calls resetLevel() itself (before and after the sanity
    // probe), so this must never change the mode: doing so would stop deaths
    // being swallowed mid-pass and the pass would die on the first impact.
    if (m_mode != Mode::Learning) {
        m_activeSim = nullptr;
    }
}

void LearnBot::onLevelExit() {
    m_mode = Mode::Off;
    m_layer = nullptr;
    m_activeSim = nullptr;
    m_lastHud.clear();
}

// -- learning -------------------------------------------------------------

void LearnBot::startLearning(PlayLayer* layer, GameSim::Advance advance) {
    if (!g_bot.noclipWhileLearning) {
        log::warn("LearnBot: learning needs 'Noclip while learning' - not starting a pass");
        m_mode = Mode::Stuck;
        return;
    }

    m_mode = Mode::Learning;
    m_learnedOnce = true;
    log::info("LearnBot: learning pass starting ({} steps already known)", m_plan.size());

    // ---- sanity probe -------------------------------------------------
    // Before spending minutes of thinking time, prove that driving the game by
    // hand actually advances it. Cheap insurance, and if it fails the console
    // says so in one line instead of quietly learning nothing.
    layer->resetLevel();

    auto playerBefore = layer->m_player1;
    float const probeStartX = playerBefore ? playerBefore->getPositionX() : 0.f;
    for (int i = 0; i < 60; ++i) {
        layer->handleButton(false, static_cast<int>(PlayerButton::Jump), true);
        advance(1.f / 240.f);
    }
    auto playerAfter = layer->m_player1;
    float const probeEndX = playerAfter ? playerAfter->getPositionX() : 0.f;
    double const probePercent = layer->getCurrentPercent();
    bool const advances = std::abs(probeEndX - probeStartX) > 1.f || probePercent > 0.01;

    layer->resetLevel();

    dbg::say(fmt::format("Test run: moved {:.0f} units in 60 steps ({:.2f}% of the level).",
        probeEndX - probeStartX, probePercent),
        advances ? dbg::Tone::Good : dbg::Tone::Bad);

    if (!advances) {
        m_activeSim = nullptr;
        dbg::say("The game did not move while I drove it, so I have stopped", dbg::Tone::Bad);
        dbg::say("instead of spending minutes learning nothing.", dbg::Tone::Bad);
        dbg::say("This is a bug in the mod, not in your level. Send this line:", dbg::Tone::Warn);
        dbg::detail(fmt::format(
            "probe: player1 {}, x {:.2f} -> {:.2f}, percent {:.3f}, isEditor {}",
            playerBefore ? "present" : "null", probeStartX, probeEndX,
            probePercent, layer->m_isEditor ? 1 : 0));
        m_mode = Mode::Stuck;
        m_blockedReason = "the game did not advance";
        m_learnedOnce = false;
        return;
    }

    // ---- the pass ------------------------------------------------------
    GameSim sim(layer, std::move(advance), 1.f / 240.f);
    m_activeSim = &sim;

    learnbot::Params params;
    params.searchBack = std::max(g_bot.searchBack, 1);
    params.maxSearchBack = std::max(g_bot.maxSearchBack, params.searchBack);
    params.lookaheadTicks = std::max(g_bot.lookaheadTicks, 10);
    params.maxTicksPerRun = std::max(g_bot.maxTicks, 2400);
    params.rewindPercent = std::max(g_bot.rewindPercent, 0.5);
    params.checkpointEvery = std::max(g_bot.checkpointEvery, 20);
    params.maxRewindsPerDeath = std::max(g_bot.maxRewinds, 1);
    params.maxDepth = std::clamp(g_bot.maxDepth, 1, 3);
    params.maxCandidates = std::max(g_bot.maxCandidates, 8);
    params.budgetTicks = budgetForEffort(g_bot.effort);

    dbg::say(fmt::format("Effort: {}/10 (budget: {} simulated steps).",
        std::clamp(g_bot.effort, 1, 10), dbg::millions(params.budgetTicks)));
    dbg::say(fmt::format("Searching up to {} steps ({}) before each crash, {} input{} at a time.",
        params.maxSearchBack, dbg::seconds(params.maxSearchBack),
        params.maxDepth, params.maxDepth == 1 ? "" : "s"));

    learnbot::Learner learner(sim, params, m_plan,
        [](learnbot::Event const& event) { dbg::report(event); });

    auto heuristic = [layer]() {
        return heur::decide(layer, layer->m_player1, g_bot);
    };

    auto const progress = learner.run(heuristic);

    m_plan = learner.plan();
    m_best = std::max(m_best, progress.best);
    m_activeSim = nullptr;

    log::info("LearnBot: pass done - {} at {:.1f}% (deaths {}, repairs {}, rewinds {}, {} steps)",
        progress.status == learnbot::Status::Complete ? "complete" : "stuck",
        progress.best, progress.deaths, progress.repairs, progress.rewinds, m_plan.size());

    dbg::section("Result");
    dbg::say(fmt::format("Best: {:.1f}% of the level.", progress.best),
        progress.status == learnbot::Status::Complete ? dbg::Tone::Good : dbg::Tone::Warn);
    dbg::say(fmt::format("Crashes seen: {}  |  fixed: {}  |  backed up and retried: {}",
        progress.deaths, progress.repairs, progress.rewinds));
    dbg::say(fmt::format("Route length: {} steps ({} of level).",
        m_plan.size(), dbg::seconds(static_cast<long long>(m_plan.size()))));
    dbg::detail(fmt::format("simulated {} steps; deepest repair changed {} inputs",
        progress.simulatedTicks, progress.deepestRepair));
    dbg::say("Now playing that route back with normal physics.", dbg::Tone::Info);

    saveRoute(layer->m_level);

    // Hand the game back to GD. The trial left the level wherever its last
    // branch ended, and checkpoint 0 holds no snapshot to load, so the honest
    // way back to the start is a normal level reset.
    m_replayTick = 0;
    layer->resetLevel();

    // Even a stuck route is replayed: it shows exactly how far the learned
    // inputs get before the heuristic takes over again.
    m_mode = Mode::Replay;
    m_learnedOnce = true;
}

// -- live replay ----------------------------------------------------------

void LearnBot::pushLiveInput(PlayLayer* layer) {
    if (m_mode != Mode::Replay || !layer || !layer->m_player1) {
        return;
    }

    // m_currentStep counts physics steps in the current attempt; it is the
    // index the plan was learned against. If a future game version changes
    // that, the "inject offset" setting shifts the whole plan.
    int const step = static_cast<int>(layer->m_currentStep) + g_bot.injectOffset;
    int const index = std::max(step, 0) + m_planIndexOffset;

    learnbot::Input input;
    if (index >= 0 && index < static_cast<int>(m_plan.size())) {
        input = m_plan[index];
    }
    else {
        // Past the learned route: fall back to the heuristic so the bot keeps
        // playing sensibly instead of freezing.
        input = heur::decide(layer, layer->m_player1, g_bot);
    }

    layer->handleButton(input == learnbot::Input::Hold, static_cast<int>(PlayerButton::Jump), true);
}

// -- route persistence ----------------------------------------------------

std::string LearnBot::routePath(GJGameLevel* level) const {
    int const id = level ? static_cast<int>(level->m_levelID) : 0;
    auto dir = Mod::get()->getSaveDir() / "routes";
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    return (dir / fmt::format("{}.plan", id)).string();
}

void LearnBot::loadRoute(GJGameLevel* level) {
    m_plan.clear();
    if (!level) {
        return;
    }

    std::ifstream file(routePath(level));
    if (!file) {
        return;
    }

    std::string encoded;
    std::getline(file, encoded);

    m_plan.reserve(encoded.size());
    for (char c : encoded) {
        m_plan.push_back(c == '1' ? learnbot::Input::Hold : learnbot::Input::Release);
    }
}

void LearnBot::saveRoute(GJGameLevel* level) const {
    if (!level || m_plan.empty()) {
        return;
    }

    std::string encoded;
    encoded.reserve(m_plan.size());
    for (auto input : m_plan) {
        encoded.push_back(input == learnbot::Input::Hold ? '1' : '0');
    }

    std::ofstream file(routePath(level), std::ios::trunc);
    if (!file) {
        log::warn("LearnBot: could not save route for level {}", static_cast<int>(level->m_levelID));
        return;
    }
    file << encoded << '\n';
}

// -- HUD ------------------------------------------------------------------

std::string LearnBot::statusText() const {
    switch (m_mode) {
        case Mode::Off:
            return g_bot.enabled ? "LearnBot: idle" : "LearnBot: off";
        case Mode::Blocked:
            return fmt::format("LearnBot: BLOCKED ({})", m_blockedReason);
        case Mode::Waiting:
            return fmt::format("LearnBot: armed ({} steps saved)", m_plan.size());
        case Mode::Learning:
            return "LearnBot: learning...";
        case Mode::Replay: {
            double const percent = m_layer ? m_layer->getCurrentPercent() : 0.0;
            return fmt::format("LearnBot: replay {:.0f}%  best {:.0f}%  {} steps",
                percent, m_best, m_plan.size());
        }
        case Mode::Stuck:
            if (!m_blockedReason.empty()) {
                return fmt::format("LearnBot: stopped ({})", m_blockedReason);
            }
            return fmt::format("LearnBot: stuck at {:.0f}%", m_best);
    }
    return "LearnBot";
}
