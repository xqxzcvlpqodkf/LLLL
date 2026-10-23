#include "Console.hpp"

#include "State.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

#ifdef GEODE_IS_WINDOWS
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

using namespace geode::prelude;

namespace dbg {

namespace {

bool g_open = false;
bool g_consoleWindow = false;
std::ofstream g_log;
int g_lastPercentPrinted = -1;

// ANSI colours, only used when the console accepted them.
char const* colourFor(Tone tone) {
    switch (tone) {
        case Tone::Good: return "\x1b[92m";   // bright green
        case Tone::Warn: return "\x1b[93m";   // yellow
        case Tone::Bad:  return "\x1b[91m";   // red
        case Tone::Info: default: return "\x1b[97m";
    }
}

std::filesystem::path logPath() {
    auto dir = Mod::get()->getSaveDir();
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    return dir / "learnbot.log";
}

void writeLine(std::string const& line, Tone tone, bool technical) {
    if (!g_open) {
        return;
    }
    if (technical && !g_bot.debugDetail) {
        return;
    }

    if (g_consoleWindow) {
        std::printf("%s%s\x1b[0m\n", colourFor(tone), line.c_str());
        std::fflush(stdout);
    }
    if (g_log.is_open()) {
        g_log << line << '\n';
        g_log.flush();
    }
    log::info("[LearnBot] {}", line);
}

} // namespace

void open() {
    if (g_open) {
        return;
    }

#ifdef GEODE_IS_WINDOWS
    if (g_bot.debugConsole && AllocConsole()) {
        SetConsoleTitleA("LearnBot - what the bot is thinking");
        // Point the C streams at the new window.
        FILE* unused = nullptr;
        freopen_s(&unused, "CONOUT$", "w", stdout);
        freopen_s(&unused, "CONOUT$", "w", stderr);
        freopen_s(&unused, "CONIN$", "r", stdin);

        // Ask for ANSI colour support.
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (handle && GetConsoleMode(handle, &mode)) {
            SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
        g_consoleWindow = true;
    }
#endif

    g_log.open(logPath(), std::ios::trunc);
    g_open = true;
}

void say(std::string const& text, Tone tone) {
    writeLine(text, tone, false);
}

void detail(std::string const& text) {
    writeLine("      " + text, Tone::Info, true);
}

void force(std::string const& text) {
    writeLine("      " + text, Tone::Warn, false);
}

void section(std::string const& title) {
    writeLine("", Tone::Info, false);
    writeLine("=== " + title + " ===", Tone::Info, false);
}

void banner() {
    section("LearnBot");
    say("What this is: it plays the level for you, and it teaches itself how.");
    say("How: it tries the level, notices where it crashes, changes the timing of");
    say("     a jump or a click, and tries again - hundreds of times a second.");
    say("");
    say("Words you will see:");
    say("  %          how far through the level it got (100% = the end).");
    say("  step/tick  one 1/240th of a second of game physics.");
    say("  orb        a ring you press in mid-air to jump again.");
    say("  pad        a spring on the floor: it fires on its own, no input needed.");
    say("  noclip     for training only: it survives hits while it is thinking.");
    say("");
    say("Rules it will not break: no medium, insane or extreme demons, and no");
    say("progress is ever saved from a run it took part in.", Tone::Warn);
    say("");
}

std::string millions(long long ticks) {
    return fmt::format("{:.1f} million", static_cast<double>(ticks) / 1'000'000.0);
}

std::string seconds(long long ticks) {
    double const value = static_cast<double>(ticks) / 240.0;
    return fmt::format("{:.2f} s", value);
}

void report(learnbot::Event const& event) {
    switch (event.kind) {
        case learnbot::EventKind::PassStarted:
            section("Thinking about this level");
            say("I am about to replay the level inside the game, as many times as");
            say("it takes. The game may freeze for a moment - that is me working.");
            break;

        case learnbot::EventKind::Reached: {
            int const whole = static_cast<int>(event.percent);
            if (whole <= g_lastPercentPrinted) {
                return;  // don't spam a line per tenth of a percent
            }
            g_lastPercentPrinted = whole;
            say(fmt::format("Getting further: {}% of the level.", whole), Tone::Good);
            break;
        }

        case learnbot::EventKind::Hit:
            say(fmt::format("Crashed at {:.1f}%. Working out what to change.",
                event.percent), Tone::Warn);
            break;

        case learnbot::EventKind::RepairAccepted:
            say(fmt::format(
                "Fixed it. I changed {} input{} and it now survives {} further.",
                event.depth, event.depth == 1 ? "" : "s", seconds(event.gainTicks)),
                Tone::Good);
            detail(fmt::format("repair at tick {} ({} into the level), depth {}, {} simulated steps so far",
                event.tick, seconds(event.tick), event.depth, event.simulated));
            break;

        case learnbot::EventKind::Rewound:
            say("Changing one or two inputs there was not enough, so I have gone");
            say("back to a little before the crash to try a different idea.", Tone::Warn);
            detail(fmt::format("rewound to {:.1f}%, {} simulated steps so far",
                event.percent, event.simulated));
            break;

        case learnbot::EventKind::Stuck:
            say(fmt::format(
                "I cannot get past {:.0f}% with the ideas I have. Saving what I",
                event.percent));
            say("learned so far - next attempt will start from there.", Tone::Bad);
            break;

        case learnbot::EventKind::Completed:
            say("Done: I can play this level from the start to the end.", Tone::Good);
            say("Saving the route so I can do it again instantly.", Tone::Good);
            break;

        case learnbot::EventKind::BudgetExhausted:
            say(fmt::format("I have used my whole thinking budget for this level ({}",
                millions(event.simulated)));
            say("simulated steps) - keeping the best route I found.", Tone::Warn);
            break;

        case learnbot::EventKind::RunLengthCap:
            say(fmt::format(
                "I reached the limit for one attempt ({} of level) without",
                seconds(event.tick)));
            say("finishing or crashing once. If the level is simply very long,",
                Tone::Warn);
            say("raise \"Max ticks per pass\" in settings. Keeping what I have.",
                Tone::Warn);
            break;
    }
}

} // namespace dbg
