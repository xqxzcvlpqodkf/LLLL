#pragma once

#include <Geode/Geode.hpp>

#include "GameSim.hpp"
#include "State.hpp"
#include "core/Gate.hpp"
#include "core/Learner.hpp"

#include <string>
#include <utility>
#include <vector>

// One way of making the game advance by one physics step. Several exist because
// which function actually runs the level logic differs between builds, and the
// bindings cannot tell us: PlayLayer overrides `update` in the real binary even
// though the header does not declare it.
struct DriveStrategy {
    char const* name = "";
    GameSim::Advance advance;
};

// Drives one level: checks the gate, learns a route, then plays it back.
class LearnBot {
public:
    static LearnBot& get();

    // -- hooks, called from Hooks.cpp ------------------------------------

    // PlayLayer::init. Decides whether this level is allowed at all.
    void onLevelStart(PlayLayer* layer);

    // PlayLayer::destroyPlayer. Returns true when the death should be
    // swallowed (noclip during a learning pass, or the optional fallback).
    bool onDeath(PlayerObject* player, GameObject* object);

    // GJBaseGameLayer::update, before the original runs.
    // Returns true if the bot took over the frame.
    bool beforeUpdate(GJBaseGameLayer* layer, std::vector<DriveStrategy> const& drives);

    // GJBaseGameLayer::update, after the original runs (live replay).
    void afterUpdate(GJBaseGameLayer* layer);

    // PlayLayer::resetLevel / level exit.
    void onReset();
    void onLevelExit();

    // -- status for the HUD ----------------------------------------------

    enum class Mode { Off, Blocked, Waiting, Learning, Replay, Stuck };
    Mode mode() const { return m_mode; }
    std::string statusText() const;
    std::string blockedReason() const { return m_blockedReason; }

    // The layer this pass is running on (null when idle).
    PlayLayer* playLayer() const { return m_layer; }

    // For the HUD to throttle redraws.
    std::string lastHudText() const { return m_lastHud; }
    void rememberHudText(std::string text) { m_lastHud = std::move(text); }

private:
    void startLearning(PlayLayer* layer, std::vector<DriveStrategy> const& drives);
    void pushLiveInput(PlayLayer* layer);

    void loadRoute(GJGameLevel* level);
    void saveRoute(GJGameLevel* level) const;
    std::string routePath(GJGameLevel* level) const;

    PlayLayer* m_layer = nullptr;
    GameSim* m_activeSim = nullptr;
    Mode m_mode = Mode::Off;

    std::vector<learnbot::Input> m_plan;
    int m_replayTick = 0;
    int m_stepsPerCall = 1;   // measured during calibration, saved with the route
    double m_best = 0.0;
    int m_planIndexOffset = 0;

    std::string m_blockedReason;
    std::string m_lastHud;
    bool m_learnedOnce = false;

    // Readiness: the frames right after PlayLayer::init are not gameplay, and
    // taking the loop over during them makes every manual update a no-op.
    int m_warmupFrames = 0;
    float m_lastPlayerX = -1e9f;
};
