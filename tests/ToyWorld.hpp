#pragma once

// A toy side-scroller that mimics Geometry Dash's cube physics closely enough
// to exercise the learner: a 240 Hz fixed tick, constant forward speed, gravity
// and a jump impulse, spikes that kill on overlap, pits you can fall into, and
// orbs you must press while touching.
//
// This exists purely so the learning algorithm and the orb/pad style rules can
// be run and verified on a PC without Geometry Dash. It is not part of the mod.

#include "../src/core/Learner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace toy {

struct Spike {
    double x = 0.0;
    double width = 30.0;
    double height = 30.0;
};

struct Pit {
    double from = 0.0;
    double to = 0.0;
};

struct Orb {
    double x = 0.0;
    double y = 0.0;
    double radius = 32.0;
};

struct Course {
    double length = 0.0;
    std::vector<Spike> spikes;
    std::vector<Pit> pits;
    std::vector<Orb> orbs;
};

// A course of spikes with a fixed gap between them.
inline Course makeCourse(double gap, int count, int doubleSpikes = 0) {
    Course course;
    double const spikeWidth = 30.0;
    double const spikeHeight = 30.0;
    double x = 300.0;

    for (int i = 0; i < count; ++i) {
        course.spikes.push_back({ x, spikeWidth, spikeHeight });
        x += spikeWidth;
        if (doubleSpikes > 0 && i % doubleSpikes == doubleSpikes - 1) {
            course.spikes.push_back({ x, spikeWidth, spikeHeight });
            x += spikeWidth;
        }
        x += gap;
    }
    course.length = x + 600.0;
    return course;
}

// A course built around one pit with a single orb over it: the pit is wider
// than one jump, so you have to press again while touching the orb in mid-air.
// Geometry is chosen so a jump taken at the pit edge reaches the orb on its arc
// and the second jump clears the far edge.
inline Course makeOrbCourse(double pitWidth = 200.0, double orbHeight = 40.0) {
    Course course;
    double const pitStart = 420.0;

    course.pits.push_back({ pitStart, pitStart + pitWidth });
    course.orbs.push_back({ pitStart + 50.0, orbHeight, 34.0 });

    // A spike after the pit, so overshooting the orb is punished too.
    course.spikes.push_back({ pitStart + pitWidth + 90.0, 30.0, 30.0 });

    course.length = pitStart + pitWidth + 700.0;
    return course;
}

class ToySim final : public learnbot::Sim {
public:
    static constexpr double kTickRate = 240.0;
    static constexpr double kDt = 1.0 / kTickRate;
    static constexpr double kSpeed = 312.0;
    static constexpr double kGravity = 2800.0;
    static constexpr double kJumpVelocity = 585.0;
    static constexpr double kCubeSize = 30.0;

    explicit ToySim(Course course) : m_course(std::move(course)) {
        m_state.orbUsed.assign(m_course.orbs.size(), 0);
        // Checkpoint 0 is always the level start.
        m_checkpoints.push_back({ m_state, 0, 0.0 });
    }

    // -- learnbot::Sim ---------------------------------------------------

    void restore(int checkpoint) override {
        auto const& snapshot = m_checkpoints.at(static_cast<std::size_t>(checkpoint));
        m_state = snapshot.state;
        m_tick = snapshot.tick;
    }

    int saveCheckpoint() override {
        m_checkpoints.push_back({ m_state, m_tick, percent() });
        return static_cast<int>(m_checkpoints.size()) - 1;
    }

    int checkpointTick(int checkpoint) const override {
        return m_checkpoints.at(static_cast<std::size_t>(checkpoint)).tick;
    }

    double checkpointPercent(int checkpoint) const override {
        return m_checkpoints.at(static_cast<std::size_t>(checkpoint)).percent;
    }

    learnbot::StepResult step(learnbot::Input input) override {
        if (m_state.dead) {
            return learnbot::StepResult::Died;
        }
        if (m_state.finished) {
            return learnbot::StepResult::Completed;
        }

        bool const holding = input == learnbot::Input::Hold;

        // Orbs: a press while touching one works even in mid-air.
        for (std::size_t i = 0; i < m_course.orbs.size(); ++i) {
            if (m_state.orbUsed[i] == 0 && holding && touchingOrb(i)) {
                m_state.vy = kJumpVelocity * 1.05;
                m_state.orbUsed[i] = 1;
                m_state.onGround = false;
                break;
            }
        }

        if (holding && m_state.onGround) {
            m_state.vy = kJumpVelocity;
            m_state.onGround = false;
        }

        m_state.vy -= kGravity * kDt;
        m_state.y += m_state.vy * kDt;
        m_state.x += kSpeed * kDt;

        bool const overPit = std::any_of(m_course.pits.begin(), m_course.pits.end(),
            [this](Pit const& pit) { return m_state.x + kCubeSize * 0.5 > pit.from
                                        && m_state.x + kCubeSize * 0.5 < pit.to; });

        if (m_state.y <= 0.0 && !overPit) {
            m_state.y = 0.0;
            m_state.vy = 0.0;
            m_state.onGround = true;
        }
        else {
            m_state.onGround = false;
        }

        ++m_tick;

        if (touchingSpike() || m_state.y < -200.0) {
            m_state.dead = true;
            return learnbot::StepResult::Died;
        }
        if (m_state.x >= m_course.length) {
            m_state.finished = true;
            return learnbot::StepResult::Completed;
        }
        return learnbot::StepResult::Alive;
    }

    int tick() const override { return m_tick; }
    double percent() const override {
        return std::max(0.0, std::min(100.0, 100.0 * m_state.x / m_course.length));
    }

    // -- state access for heuristics and tests ---------------------------

    double x() const { return m_state.x; }
    double y() const { return m_state.y; }
    bool onGround() const { return m_state.onGround; }

    double distanceToNextSpike() const {
        double best = 1e9;
        for (auto const& spike : m_course.spikes) {
            double const d = spike.x - (m_state.x + kCubeSize);
            if (d > -kCubeSize) {
                best = std::min(best, d);
            }
        }
        return best;
    }

    // Distance from the cube's right edge to the next pit edge ahead.
    double distanceToNextPit() const {
        double best = 1e9;
        for (auto const& pit : m_course.pits) {
            double const d = pit.from - (m_state.x + kCubeSize);
            if (d > -kCubeSize) {
                best = std::min(best, d);
            }
        }
        return best;
    }

    // Distance to the next unused orb, or a huge number if there is none.
    double distanceToNextOrb() const {
        double best = 1e9;
        for (std::size_t i = 0; i < m_course.orbs.size(); ++i) {
            if (m_state.orbUsed[i]) {
                continue;
            }
            double const dx = m_course.orbs[i].x - m_state.x;
            double const dy = m_course.orbs[i].y - m_state.y;
            best = std::min(best, std::sqrt(dx * dx + dy * dy));
        }
        return best;
    }

    double nextOrbX() const {
        double best = 1e9;
        for (std::size_t i = 0; i < m_course.orbs.size(); ++i) {
            if (!m_state.orbUsed[i]) {
                best = std::min(best, m_course.orbs[i].x);
            }
        }
        return best;
    }

private:
    struct State {
        double x = 0.0;
        double y = 0.0;
        double vy = 0.0;
        bool onGround = true;
        bool dead = false;
        bool finished = false;
        std::vector<char> orbUsed;
    };

    struct Snapshot {
        State state;
        int tick = 0;
        double percent = 0.0;
    };

    bool touchingOrb(std::size_t index) const {
        auto const& orb = m_course.orbs[index];
        double const dx = orb.x - m_state.x;
        double const dy = orb.y - m_state.y;
        return std::sqrt(dx * dx + dy * dy) <= orb.radius;
    }

    bool touchingSpike() const {
        double const margin = 4.0;
        double const left = m_state.x + margin;
        double const right = m_state.x + kCubeSize - margin;
        double const bottom = m_state.y;
        double const top = m_state.y + kCubeSize;

        for (auto const& spike : m_course.spikes) {
            double const sLeft = spike.x;
            double const sRight = spike.x + spike.width;
            if (right > sLeft && left < sRight && top > 0.0 && bottom < spike.height) {
                return true;
            }
        }
        return false;
    }

    Course m_course;
    State m_state;
    std::vector<Snapshot> m_checkpoints;
    int m_tick = 0;
};

} // namespace toy
