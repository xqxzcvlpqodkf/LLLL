#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>

#include "Console.hpp"
#include "State.hpp"

using namespace geode::prelude;

namespace {

    template <class T, class F>
    void bind(char const* key, F apply) {
        apply(Mod::get()->getSettingValue<T>(key));
        listenForSettingChanges<T>(key, apply);
    }

}

$on_mod(Loaded) {
    bind<bool>("enabled", [](bool v) { g_bot.enabled = v; });

    // Gate
    bind<bool>("block-easy-demon", [](bool v) { g_bot.blockEasyDemon = v; });
    bind<bool>("block-hard-demon", [](bool v) { g_bot.blockHardDemon = v; });
    bind<bool>("block-medium-demon", [](bool v) { g_bot.blockMediumDemon = v; });
    bind<bool>("block-insane-demon", [](bool v) { g_bot.blockInsaneDemon = v; });
    bind<bool>("block-extreme-demon", [](bool v) { g_bot.blockExtremeDemon = v; });
    bind<bool>("block-completed-levels", [](bool v) { g_bot.blockCompletedLevels = v; });

    // Learning
    bind<int64_t>("search-back", [](int64_t v) { g_bot.searchBack = static_cast<int>(v); });
    bind<int64_t>("lookahead-ticks", [](int64_t v) { g_bot.lookaheadTicks = static_cast<int>(v); });
    bind<double>("rewind-percent", [](double v) { g_bot.rewindPercent = v; });
    bind<int64_t>("checkpoint-every", [](int64_t v) { g_bot.checkpointEvery = static_cast<int>(v); });
    bind<int64_t>("max-ticks", [](int64_t v) { g_bot.maxTicks = static_cast<int>(v); });
    bind<int64_t>("max-rewinds", [](int64_t v) { g_bot.maxRewinds = static_cast<int>(v); });

    // How hard it thinks
    bind<int64_t>("effort", [](int64_t v) { g_bot.effort = static_cast<int>(v); });
    bind<int64_t>("max-search-back", [](int64_t v) { g_bot.maxSearchBack = static_cast<int>(v); });
    bind<int64_t>("max-depth", [](int64_t v) { g_bot.maxDepth = static_cast<int>(v); });
    bind<int64_t>("max-candidates", [](int64_t v) { g_bot.maxCandidates = static_cast<int>(v); });

    // Debugging
    bind<bool>("debug-console", [](bool v) { g_bot.debugConsole = v; });
    bind<bool>("debug-detail", [](bool v) { g_bot.debugDetail = v; });

    // Behaviour
    bind<bool>("noclip-while-learning", [](bool v) { g_bot.noclipWhileLearning = v; });
    bind<bool>("noclip-fallback", [](bool v) { g_bot.noclipFallback = v; });
    bind<bool>("safe-mode", [](bool v) { g_bot.safeMode = v; });
    bind<bool>("show-hud", [](bool v) { g_bot.showHud = v; });
    bind<int64_t>("inject-offset", [](int64_t v) { g_bot.injectOffset = static_cast<int>(v); });

    // Heuristic
    bind<double>("jump-clearance-min", [](double v) { g_bot.jumpClearanceMin = static_cast<float>(v); });
    bind<double>("jump-clearance-max", [](double v) { g_bot.jumpClearanceMax = static_cast<float>(v); });
    bind<double>("flying-lookahead", [](double v) { g_bot.flyingLookahead = static_cast<float>(v); });
    bind<double>("orb-reach", [](double v) { g_bot.orbReach = static_cast<float>(v); });
    bind<double>("pad-avoid-distance", [](double v) { g_bot.padAvoidDistance = static_cast<float>(v); });
    bind<double>("pit-jump-distance", [](double v) { g_bot.pitJumpDistance = static_cast<float>(v); });

    log::info("LearnBot loaded. Gate: medium={} insane={} extreme={} (blocked), easy={} hard={}",
        g_bot.blockMediumDemon, g_bot.blockInsaneDemon, g_bot.blockExtremeDemon,
        g_bot.blockEasyDemon, g_bot.blockHardDemon);

    // Settings are bound by now, so the console toggle is accurate.
    if (g_bot.debugConsole) {
        dbg::open();
        dbg::banner();
    }
}

$on_game(Loaded) {
    listenForKeybindSettingPresses("toggle-keybind",
        [](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat) {
                return;
            }
            bool const enabled = !Mod::get()->getSettingValue<bool>("enabled");
            Mod::get()->setSettingValue<bool>("enabled", enabled);
            Notification::create(enabled ? "LearnBot armed" : "LearnBot disarmed",
                NotificationIcon::Info)->show();
        });
}
