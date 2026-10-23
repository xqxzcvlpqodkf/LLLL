#pragma once

#include <Geode/Geode.hpp>

#include "core/Learner.hpp"

#include <functional>
#include <vector>

// Adapts a live PlayLayer to the learner's Sim interface.
//
// The important trick: the mod takes over the game loop while it is learning.
// Instead of letting GD advance the frame, the bot calls the original
// GJBaseGameLayer::update() itself, one 240 Hz tick at a time, with no
// rendering in between. That turns "play the level" into a synchronous
// function call, which is what lets the learner rewind, re-simulate a branch
// and compare outcomes - the same way the frame-window scanners replay a level
// thousands of times.
class GameSim final : public learnbot::Sim {
public:
    using Advance = std::function<void(float)>;

    GameSim(PlayLayer* layer, Advance advance, float dt)
        : m_layer(layer), m_advance(std::move(advance)), m_dt(dt) {
        m_checkpoints.push_back({ nullptr, 0, 0.0 });  // checkpoint 0 = level start
    }

    ~GameSim() override {
        for (auto& checkpoint : m_checkpoints) {
            if (checkpoint.object) {
                checkpoint.object->release();
            }
        }
    }

    void restore(int checkpoint) override {
        if (checkpoint < 0 || checkpoint >= static_cast<int>(m_checkpoints.size())) {
            return;
        }
        auto const& snapshot = m_checkpoints[checkpoint];
        if (snapshot.object && m_layer) {
            m_layer->loadFromCheckpoint(snapshot.object);
        }
        m_tick = snapshot.tick;
        m_died = false;
    }

    int saveCheckpoint() override {
        if (!m_layer) {
            return 0;
        }
        auto object = m_layer->markCheckpoint();
        if (object) {
            object->retain();
        }
        m_checkpoints.push_back({ object, m_tick, percent() });
        return static_cast<int>(m_checkpoints.size()) - 1;
    }

    int checkpointTick(int checkpoint) const override {
        return m_checkpoints.at(static_cast<std::size_t>(checkpoint)).tick;
    }

    double checkpointPercent(int checkpoint) const override {
        return m_checkpoints.at(static_cast<std::size_t>(checkpoint)).percent;
    }

    learnbot::StepResult step(learnbot::Input input) override {
        if (!m_layer) {
            return learnbot::StepResult::Died;
        }

        m_died = false;
        m_layer->handleButton(input == learnbot::Input::Hold, kJumpButton, true);
        m_advance(m_dt);
        ++m_tick;

        if (m_died) {
            return learnbot::StepResult::Died;
        }
        if (m_layer->m_levelEndAnimationStarted || m_layer->getCurrentPercent() >= 100.f) {
            return learnbot::StepResult::Completed;
        }
        return learnbot::StepResult::Alive;
    }

    int tick() const override { return m_tick; }

    double percent() const override {
        return m_layer ? static_cast<double>(m_layer->getCurrentPercent()) : 0.0;
    }

    // Called from the destroyPlayer hook while the bot is driving.
    void markDied() { m_died = true; }

    void resetToStart() {
        restore(0);
        m_tick = 0;
    }

private:
    static constexpr int kJumpButton = static_cast<int>(PlayerButton::Jump);

    struct Snapshot {
        CheckpointObject* object = nullptr;
        int tick = 0;
        double percent = 0.0;
    };

    PlayLayer* m_layer = nullptr;
    Advance m_advance;
    float m_dt = 1.f / 240.f;
    std::vector<Snapshot> m_checkpoints;
    int m_tick = 0;
    bool m_died = false;
};
