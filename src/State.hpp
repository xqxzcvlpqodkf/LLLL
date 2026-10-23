#pragma once

#include <Geode/Geode.hpp>

// Cached mod settings, refreshed by the listeners in main.cpp.
struct BotSettings {
    bool enabled = false;

    // Gate (defaults = "never run on medium, insane or extreme demons")
    bool blockEasyDemon = false;
    bool blockHardDemon = false;
    bool blockMediumDemon = true;
    bool blockInsaneDemon = true;
    bool blockExtremeDemon = true;
    bool blockCompletedLevels = false;

    // Learning
    int searchBack = 24;
    int lookaheadTicks = 96;
    double rewindPercent = 2.0;
    int checkpointEvery = 120;
    int maxTicks = 120 * 240;
    int maxRewinds = 24;

    // Behaviour
    bool noclipWhileLearning = true;
    bool noclipFallback = false;
    bool safeMode = true;
    bool showHud = true;
    int injectOffset = 0;
    bool autoReplay = true;

    // How hard it thinks
    int effort = 6;              // 1..10, maps to a simulation budget
    int maxSearchBack = 480;     // widest search window, in ticks
    int maxDepth = 2;            // inputs that may change at once
    int maxCandidates = 128;     // ideas tested per death

    // Debugging
    bool debugConsole = true;    // separate console window / log file
    bool debugDetail = false;    // also print the technical lines

    // Heuristic tuning
    float jumpClearanceMin = 12.f;
    float jumpClearanceMax = 60.f;
    float flyingLookahead = 140.f;
    float hullWidth = 30.f;
    float orbReach = 18.f;            // how close counts as "touching an orb"
    float padAvoidDistance = 90.f;    // don't jump this close to a jump pad
    float pitJumpDistance = 40.f;     // jump when the ground ends this soon
};

inline BotSettings g_bot;
