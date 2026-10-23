#include "Bot.hpp"

#include "Console.hpp"
#include "Heuristic.hpp"

#include <algorithm>
#include <limits>
#include <filesystem>
#include <string>
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

// Step candidates, smallest first. A game that refuses a step of exactly
// 1/240 s (float rounding against an internal accumulator, or a minimum-step
// rule) will accept the next one up, so we find the smallest step that works
// instead of assuming the textbook value.
constexpr float kStepCandidates[] = {
    1.f / 240.f,
    1.f / 240.f * 1.0001f,
    1.f / 239.f,
    1.f / 238.f,
    1.f / 235.f,
    1.f / 228.f,
    1.f / 200.f,
    1.f / 120.f,
    1.f / 60.f,
    1.f / 30.f,
};

struct Calibration {
    bool works = false;
    float dt = 1.f / 240.f;
    int stepsPerCall = 1;
    int stepsSeen = 0;
    float moved = 0.f;
};

// Drives the game `calls` times and reports what actually happened, using both
// the game's own step counter and the player's position as evidence.
Calibration probeSteps(PlayLayer* layer, GameSim::Advance const& advance, float dt, int calls) {
    Calibration result;
    result.dt = dt;

    int const before = layer->m_player1 ? static_cast<int>(layer->m_currentStep) : 0;
    float const x0 = layer->m_player1 ? layer->m_player1->getPositionX() : 0.f;

    for (int i = 0; i < calls; ++i) {
        advance(dt);
    }

    int const after = layer->m_player1 ? static_cast<int>(layer->m_currentStep) : 0;
    float const x1 = layer->m_player1 ? layer->m_player1->getPositionX() : 0.f;

    result.stepsSeen = after - before;
    result.moved = x1 - x0;
    result.stepsPerCall = result.stepsSeen > 0 ? std::max(result.stepsSeen / std::max(calls, 1), 1) : 0;
    result.works = result.stepsSeen > 0 || std::abs(result.moved) > 0.5f;
    return result;
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

bool LearnBot::beforeUpdate(GJBaseGameLayer* layer, std::vector<DriveStrategy> const& drives) {
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
        if (player) {
            m_lastPlayerX = player->getPositionX();
        }
        // Let the level build its objects and settle for a handful of frames,
        // then start. Waiting for the player to move on its own is wrong: the
        // whole point is that it only moves once we drive it.
        if (!player || ++m_warmupFrames < 5) {
            return false;
        }

        startLearning(m_layer, drives);
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

void LearnBot::startLearning(PlayLayer* layer, std::vector<DriveStrategy> const& drives) {
    if (!g_bot.noclipWhileLearning) {
        log::warn("LearnBot: learning needs 'Noclip while learning' - not starting a pass");
        m_mode = Mode::Stuck;
        return;
    }

    m_mode = Mode::Learning;
    m_learnedOnce = true;
    log::info("LearnBot: learning pass starting ({} steps already known)", m_plan.size());

    // ---- find a way to actually move the game ---------------------------
    // Two unknowns are tested together, and every combination is reported:
    // which function runs the level logic, and whether this build accepts a
    // step of exactly 1/240 s. Nothing here resets the level: a freshly reset
    // level sits in its respawn state and does not simulate at all.
    Calibration calibration;
    char const* workingStrategy = "none";

    dbg::section("Testing ways to drive the game");

    for (auto const& drive : drives) {
        float bestDt = 0.f;
        Calibration best;
        int bestSteps = 0;

        for (float dt : kStepCandidates) {
            auto attempt = probeSteps(layer, drive.advance, dt, 8);
            if (attempt.stepsSeen > bestSteps || !best.works) {
                if (attempt.works) {
                    best = attempt;
                    bestSteps = attempt.stepsSeen;
                    bestDt = dt;
                }
            }
            if (attempt.works) {
                break;  // smallest working step for this strategy
            }
        }

        if (best.works) {
            dbg::say(fmt::format("  {} -> works ({:.0f} units, {} steps)", drive.name,
                best.moved, best.stepsSeen), dbg::Tone::Good);
            if (!calibration.works) {
                calibration = best;
                workingStrategy = drive.name;
            }
        }
        else {
            dbg::say(fmt::format("  {} -> nothing happened", drive.name), dbg::Tone::Warn);
        }
        (void)bestDt;
    }

    dbg::say(fmt::format("Test run: {} steps, moved {:.0f} units.",
        calibration.works ? calibration.stepsSeen : 0, calibration.moved),
        calibration.works ? dbg::Tone::Good : dbg::Tone::Bad);

    if (!calibration.works) {
        m_activeSim = nullptr;
        dbg::say("None of the ways I know of to drive the game worked, so I have", dbg::Tone::Bad);
        dbg::say("stopped instead of spending minutes learning nothing.", dbg::Tone::Bad);
        dbg::say("This is a bug in the mod, not in your level. Please send these lines:", dbg::Tone::Warn);
        dbg::force(fmt::format(
            "diag: player1 {}, x {:.2f}, step {}, percent {:.2f}, gameState {}, editor {}, paused {}, ended {}",
            layer->m_player1 ? "present" : "null",
            layer->m_player1 ? layer->m_player1->getPositionX() : 0.f,
            static_cast<int>(layer->m_currentStep),
            layer->getCurrentPercent(),
            static_cast<int>(layer->m_gameState),
            layer->m_isEditor ? 1 : 0,
            layer->m_isPaused ? 1 : 0,
            layer->m_levelEndAnimationStarted ? 1 : 0));
        m_mode = Mode::Stuck;
        m_blockedReason = "could not drive the game";
        m_learnedOnce = false;
        return;
    }

    // Use the winner for the whole pass.
    GameSim::Advance advance;
    for (auto const& drive : drives) {
        if (std::string(drive.name) == workingStrategy) {
            advance = drive.advance;
            break;
        }
    }

    m_stepsPerCall = calibration.stepsPerCall;
    dbg::say(fmt::format("Using \"{}\" - one call = {} physics step{} (step size {:.6f} s).",
        workingStrategy, m_stepsPerCall, m_stepsPerCall == 1 ? "" : "s", calibration.dt),
        dbg::Tone::Good);

    // ---- can we rewind? -------------------------------------------------
    // The whole search depends on restoring a saved game state. If this build
    // refuses to restore one (checkpoints are normally a practice-mode feature),
    // the bot still works - it just has to restart from the beginning every time
    // instead of rewinding two percent. Say so plainly rather than dying.
    bool checkpointsWork = false;
    {
        double const percentBefore = layer->getCurrentPercent();
        auto checkpoint = layer->markCheckpoint();
        if (checkpoint) {
            checkpoint->retain();
        }

        for (int i = 0; i < 24; ++i) {
            advance(calibration.dt);
        }
        double const percentAfter = layer->getCurrentPercent();

        if (checkpoint) {
            layer->loadFromCheckpoint(checkpoint);
            checkpoint->release();
        }
        double const percentRestored = layer->getCurrentPercent();

        checkpointsWork = percentAfter > percentBefore + 0.001
            && std::abs(percentRestored - percentBefore) < 0.5;

        dbg::force(fmt::format(
            "diag: checkpoint test - {:.3f}% -> {:.3f}% -> restored {:.3f}% ({})",
            percentBefore, percentAfter, percentRestored,
            checkpointsWork ? "usable" : "NOT usable"));
    }

    if (!checkpointsWork) {
        dbg::say("Rewinding is not available on this setup, so instead of going", dbg::Tone::Warn);
        dbg::say("back 2% before each crash I will restart the level each time.", dbg::Tone::Warn);
        dbg::say("Learning still works, it is just slower.", dbg::Tone::Warn);
    }

    if (g_bot.diagnoseOnly) {
        dbg::section("Diagnose only");
        dbg::say("Stopping here as asked - nothing is being learned or played.", dbg::Tone::Info);
        dbg::say(fmt::format("Checkpoint rewind: {}.", checkpointsWork ? "works" : "not available"),
            checkpointsWork ? dbg::Tone::Good : dbg::Tone::Warn);
        m_mode = Mode::Stuck;
        m_blockedReason = "diagnose only";
        return;
    }

    // ---- the pass ------------------------------------------------------
    GameSim sim(layer, std::move(advance), calibration.dt, m_stepsPerCall);
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

    if (!checkpointsWork) {
        // No rewind points: every branch starts from the level start.
        params.checkpointEvery = std::numeric_limits<int>::max() / 4;
    }

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

    // Never persist a pass that proved nothing: 0% with no crashes is the
    // signature of a drive that did not move, and saving it poisons the level.
    bool const worthSaving = progress.deaths > 0 || progress.best >= 0.5;
    if (worthSaving) {
        saveRoute(layer->m_level);
    }
    else {
        dbg::say("Nothing worth saving from that attempt - keeping the old route.", dbg::Tone::Warn);
    }

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
    // The plan is indexed in the steps the pass actually ran; if the game only
    // accepts bigger steps, several physics ticks happen per plan entry.
    int const step = static_cast<int>(layer->m_currentStep) / std::max(m_stepsPerCall, 1)
        + g_bot.injectOffset;
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

    // Routes are versioned. Anything without the current header is rejected on
    // purpose: an older build could write a route that was never validated
    // (28800 steps of nothing), and silently replaying that is worse than
    // starting from scratch.
    std::string header;
    std::getline(file, header);
    if (header.rfind("LBROUTE2 ", 0) != 0) {
        dbg::say("Ignoring an old saved route for this level - it predates the fix.", dbg::Tone::Warn);
        return;
    }

    int stepsPerCall = 1;
    try {
        stepsPerCall = std::stoi(header.substr(9));
    }
    catch (...) {
        dbg::say("Ignoring a damaged saved route for this level.", dbg::Tone::Warn);
        return;
    }
    m_stepsPerCall = std::clamp(stepsPerCall, 1, 8);

    std::string encoded;
    std::getline(file, encoded);
    if (encoded.empty()) {
        return;
    }

    m_plan.reserve(encoded.size());
    for (char c : encoded) {
        if (c != '0' && c != '1') {
            dbg::say("Ignoring a damaged saved route for this level.", dbg::Tone::Warn);
            m_plan.clear();
            return;
        }
        m_plan.push_back(c == '1' ? learnbot::Input::Hold : learnbot::Input::Release);
    }

    // A route longer than one attempt could ever be is nonsense.
    auto const maxSensible = static_cast<std::size_t>(std::max(g_bot.maxTicks, 2400) / m_stepsPerCall + 240);
    if (m_plan.size() > maxSensible) {
        dbg::say("Ignoring an oversize saved route for this level.", dbg::Tone::Warn);
        m_plan.clear();
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
    file << "LBROUTE2 " << std::max(m_stepsPerCall, 1) << '\n';
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
