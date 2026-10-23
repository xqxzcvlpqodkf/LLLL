#pragma once

// LearnBot core, version 2: a budgeted, best-first input learner.
//
// No Geometry Dash or Geode dependency, so the exact same code runs against the
// toy simulator in tests/ and against the real game in src/GameSim.hpp.
//
// What changed from v1 and why:
//   * A simulation *budget* instead of a hard tick cap, so "spend ten minutes
//     getting this right" becomes a number the user can dial.
//   * Repair candidates are scored by how far they survive, not just by
//     "did it pass". That turns the search into hill climbing: if nothing
//     survives the whole lookahead, the candidate that survives longest is
//     still progress worth keeping.
//   * Several inputs can change at once (search depth 2 by default), because
//     plenty of real deaths need two adjusted inputs, not one.
//
// One rule keeps the whole thing honest: a trial and the commit that follows it
// must replay through the *same* function, with the same flips, or the bot would
// keep "fixing" a branch it cannot actually reproduce. Every input the learner
// simulates therefore goes through inputAt().

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace learnbot {

enum class Input : std::uint8_t { Release = 0, Hold = 1 };

constexpr Input flip(Input in) {
    return in == Input::Hold ? Input::Release : Input::Hold;
}

enum class StepResult : std::uint8_t { Alive, Died, Completed };

// The game, seen through the smallest interface the learner needs.
class Sim {
public:
    virtual ~Sim() = default;

    virtual void restore(int checkpoint) = 0;
    virtual int saveCheckpoint() = 0;
    virtual int checkpointTick(int checkpoint) const = 0;
    virtual double checkpointPercent(int checkpoint) const = 0;
    virtual StepResult step(Input input) = 0;
    virtual int tick() const = 0;
    virtual double percent() const = 0;
};

// Things worth telling the player about. The core never produces user-facing
// text: the console layer turns these into plain English.
enum class EventKind : std::uint8_t {
    PassStarted,
    Reached,
    Hit,
    RepairAccepted,
    Rewound,
    Stuck,
    Completed,
    BudgetExhausted,   // ran out of simulated ticks
    RunLengthCap,      // reached maxTicksPerRun
};

struct Event {
    EventKind kind = EventKind::PassStarted;
    double percent = 0.0;
    int tick = 0;
    int depth = 0;
    int gainTicks = 0;
    long long simulated = 0;
};

using EventSink = std::function<void(Event const&)>;

struct Params {
    int searchBack = 32;            // ticks before a hit that may change
    int maxSearchBack = 480;        // widened to this when nothing near the hit works
    int lookaheadTicks = 120;       // survival needed past the hit to be "solved"
    int maxDepth = 2;               // inputs that may change at once
    int maxCandidates = 128;        // ideas tested per death
    long long budgetTicks = 25'000'000;  // total simulated ticks (240 = 1 second)
    double rewindPercent = 2.0;     // "restart from percent - 2"
    int checkpointEvery = 120;
    int maxRewindsPerDeath = 48;
    int minGainTicks = 8;           // a partial fix must gain at least this much
    int maxTicksPerRun = 600 * 240;
    std::uint32_t seed = 0x9E3779B9u;
};

enum class Status : std::uint8_t { Idle, Learning, Repairing, Complete, Stuck };

struct Progress {
    Status status = Status::Idle;
    double best = 0.0;
    int deaths = 0;
    int repairs = 0;
    int rewinds = 0;
    int deepestRepair = 0;
    int lastFixedTick = -1;
    long long simulatedTicks = 0;
    bool outOfBudget = false;
};

class Learner {
public:
    Learner(Sim& sim, Params params = {}, std::vector<Input> plan = {}, EventSink sink = {})
        : m_sim(sim), m_params(std::move(params)), m_plan(std::move(plan)),
          m_sink(std::move(sink)), m_rng(m_params.seed ? m_params.seed : 1u) {}

    Progress const& progress() const { return m_progress; }
    std::vector<Input> const& plan() const { return m_plan; }
    void setPlan(std::vector<Input> plan) { m_plan = std::move(plan); }

    template <class Heuristic>
    Progress run(Heuristic&& heuristic) {
        m_progress = {};
        m_progress.status = Status::Learning;
        restart(0, 0);
        resetCheckpoints();
        emit(Event { EventKind::PassStarted });

        int rewindsHere = 0;
        double lastHit = -1.0;

        while (true) {
            if (outOfBudget() || m_cursor >= m_params.maxTicksPerRun) {
                m_progress.status = Status::Stuck;
                m_progress.outOfBudget = outOfBudget();
                emit(Event {
                    outOfBudget() ? EventKind::BudgetExhausted : EventKind::RunLengthCap,
                    m_progress.best, m_cursor
                });
                return m_progress;
            }

            Input const input = inputAt(m_cursor, heuristic, nullptr, true);
            StepResult const result = stepCounted(input);
            ++m_cursor;

            if (m_sim.percent() > m_progress.best + 0.5) {
                m_progress.best = m_sim.percent();
                emit(Event { EventKind::Reached, m_progress.best, m_cursor });
            }
            else {
                m_progress.best = std::max(m_progress.best, m_sim.percent());
            }

            if (result == StepResult::Completed) {
                m_progress.status = Status::Complete;
                emit(Event { EventKind::Completed, 100.0, m_cursor });
                return m_progress;
            }
            if (result == StepResult::Alive) {
                if (m_cursor % std::max(m_params.checkpointEvery, 1) == 0) {
                    saveCheckpoint();
                }
                continue;
            }

            // A hit on the tick just simulated.
            m_progress.deaths++;
            int const hitTick = m_cursor - 1;
            double const hitPercent = m_sim.percent();
            emit(Event { EventKind::Hit, hitPercent, hitTick });

            if (hitPercent > lastHit + 0.01) {
                rewindsHere = 0;
            }
            lastHit = hitPercent;

            if (tryRepair(hitTick, heuristic)) {
                continue;
            }

            if (rewindsHere >= m_params.maxRewindsPerDeath) {
                m_progress.status = Status::Stuck;
                emit(Event { EventKind::Stuck, hitPercent, hitTick });
                return m_progress;
            }
            ++rewindsHere;
            ++m_progress.rewinds;
            rewindBeforeHit(hitPercent, rewindsHere - 1);
            emit(Event { EventKind::Rewound, m_sim.percent(), m_cursor });
        }
    }

private:
    struct Checkpoint {
        int id = 0;
        int tick = 0;
        double percent = 0.0;
    };

    struct Candidate {
        std::vector<int> flips;
        int survivedTo = -1;
        bool completed = false;
    };

    // -- plumbing --------------------------------------------------------

    void emit(Event event) {
        event.simulated = m_progress.simulatedTicks;
        if (m_sink) {
            m_sink(event);
        }
    }

    bool outOfBudget() const {
        return m_progress.simulatedTicks >= m_params.budgetTicks;
    }

    StepResult stepCounted(Input input) {
        ++m_progress.simulatedTicks;
        return m_sim.step(input);
    }

    // The single place an input is decided. `flips` toggles specific ticks;
    // `record` writes the decision into the plan when the tick has no entry.
    template <class Heuristic>
    Input inputAt(int tick, Heuristic& heuristic, std::vector<int> const* flips, bool record) {
        bool const forced = flips
            && std::find(flips->begin(), flips->end(), tick) != flips->end();

        Input input;
        if (tick >= 0 && tick < static_cast<int>(m_plan.size())) {
            input = m_plan[tick];
        }
        else {
            input = heuristic();
            if (record) {
                while (static_cast<int>(m_plan.size()) < tick) {
                    m_plan.push_back(Input::Release);
                }
                if (static_cast<int>(m_plan.size()) == tick) {
                    m_plan.push_back(input);
                }
            }
        }

        if (forced) {
            input = flip(input);
            if (record) {
                while (static_cast<int>(m_plan.size()) <= tick) {
                    m_plan.push_back(Input::Release);
                }
                m_plan[tick] = input;
            }
        }
        return input;
    }

    void saveCheckpoint() {
        int const id = m_sim.saveCheckpoint();
        m_checkpoints.push_back({ id, m_cursor, m_sim.percent() });
    }

    void resetCheckpoints() {
        m_checkpoints.clear();
        m_checkpoints.push_back({ 0, 0, 0.0 });
    }

    int checkpointAtOrBefore(int tick) const {
        int best = 0;
        for (auto const& cp : m_checkpoints) {
            if (cp.tick <= tick) {
                best = cp.id;
            }
        }
        return best;
    }

    int checkpointTickOf(int id) const {
        for (auto const& cp : m_checkpoints) {
            if (cp.id == id) {
                return cp.tick;
            }
        }
        return 0;
    }

    void trimCheckpointsAfter(int tick) {
        m_checkpoints.erase(
            std::remove_if(m_checkpoints.begin(), m_checkpoints.end(),
                [tick](Checkpoint const& cp) { return cp.tick > tick; }),
            m_checkpoints.end()
        );
    }

    void restart(int checkpoint, int tick) {
        m_sim.restore(checkpoint);
        m_cursor = tick;
    }

    // Steps forward to `target` (exclusive). Returns false if the run ended.
    template <class Heuristic>
    bool runTo(int target, Heuristic& heuristic, std::vector<int> const* flips, bool record) {
        while (m_cursor < target) {
            if (outOfBudget()) {
                return false;
            }
            Input const input = inputAt(m_cursor, heuristic, flips, record);
            StepResult const result = stepCounted(input);
            ++m_cursor;
            if (result != StepResult::Alive) {
                return false;
            }
        }
        return true;
    }

    // -- repair ----------------------------------------------------------

    std::vector<std::vector<int>> buildCandidates(int hitTick, int window) {
        std::vector<std::vector<int>> candidates;

        std::vector<int> usable;  // ticks we may flip, nearest the hit first
        for (int back = 1; back <= std::max(window, 1); ++back) {
            int const tick = hitTick - back;
            if (tick > 0) {
                usable.push_back(tick);
            }
        }
        if (usable.empty()) {
            return candidates;
        }

        for (int tick : usable) {
            candidates.push_back({ tick });
        }

        if (m_params.maxDepth >= 2) {
            for (std::size_t a = 0; a < usable.size(); ++a) {
                for (std::size_t b = a + 1; b < usable.size() && b <= a + 6; ++b) {
                    candidates.push_back({ usable[b], usable[a] });
                }
            }
        }
        if (m_params.maxDepth >= 3) {
            for (std::size_t a = 0; a < usable.size(); ++a) {
                for (std::size_t b = a + 1; b < usable.size() && b <= a + 4; ++b) {
                    for (std::size_t c = b + 1; c < usable.size() && c <= b + 3; ++c) {
                        candidates.push_back({ usable[c], usable[b], usable[a] });
                    }
                }
            }
        }

        if (candidates.size() > static_cast<std::size_t>(m_params.maxCandidates)) {
            // Keep the singles (they are the cheap, most likely fixes), shuffle
            // the rest so a capped run still explores a spread of ideas.
            std::size_t const singles = std::min(usable.size(), candidates.size());
            for (std::size_t i = singles; i < candidates.size(); ++i) {
                std::size_t const j = singles + (nextRandom() % (candidates.size() - singles));
                std::swap(candidates[i], candidates[j]);
            }
            candidates.resize(static_cast<std::size_t>(m_params.maxCandidates));
        }
        return candidates;
    }

    // Simulates one candidate and reports how far it got.
    template <class Heuristic>
    Candidate evaluate(std::vector<int> flips, int hitTick, Heuristic& heuristic) {
        Candidate candidate { std::move(flips), -1, false };
        if (candidate.flips.empty()) {
            return candidate;
        }

        int const first = *std::min_element(candidate.flips.begin(), candidate.flips.end());
        int const checkpoint = checkpointAtOrBefore(first);
        restart(checkpoint, checkpointTickOf(checkpoint));
        if (!runTo(first, heuristic, nullptr, false)) {
            return candidate;  // the prefix itself no longer survives
        }

        int const end = hitTick + std::max(m_params.lookaheadTicks, 1);
        while (m_cursor < end) {
            if (outOfBudget()) {
                break;
            }
            int const tick = m_cursor;
            Input const input = inputAt(tick, heuristic, &candidate.flips, false);
            StepResult const result = stepCounted(input);
            ++m_cursor;

            if (result == StepResult::Completed) {
                candidate.completed = true;
                candidate.survivedTo = tick;
                break;
            }
            candidate.survivedTo = tick;
            if (result == StepResult::Died) {
                break;
            }
        }
        return candidate;
    }

    template <class Heuristic>
    bool tryRepair(int hitTick, Heuristic& heuristic) {
        m_progress.status = Status::Repairing;

        int const solvedAt = hitTick + m_params.lookaheadTicks;
        int const partialAt = hitTick + std::max(m_params.minGainTicks, 1);

        Candidate best;
        bool accepted = false;

        // Start with the ticks just before the hit, then widen. The cause of a
        // death is often long before it: miss an orb and you fall into the pit
        // a hundred ticks later, so searching only the last few ticks finds
        // nothing at all.
        for (int window = std::max(m_params.searchBack, 1);
             window <= std::max(m_params.maxSearchBack, m_params.searchBack);
             window *= 2) {

            auto candidates = buildCandidates(hitTick, window);
            if (candidates.empty()) {
                break;
            }

            for (auto& flips : candidates) {
                if (outOfBudget()) {
                    break;
                }
                auto candidate = evaluate(std::move(flips), hitTick, heuristic);
                if (candidate.survivedTo < 0) {
                    continue;
                }
                bool const better = candidate.survivedTo > best.survivedTo
                    || (candidate.survivedTo == best.survivedTo
                        && candidate.flips.size() < best.flips.size());
                if (better) {
                    best = std::move(candidate);
                }
                if (best.completed || best.survivedTo >= solvedAt) {
                    break;
                }
            }

            bool const solved = best.completed || best.survivedTo >= solvedAt;
            bool const partial = !best.flips.empty() && best.survivedTo >= partialAt;
            if (solved || partial) {
                accepted = true;
                break;
            }
            if (outOfBudget()) {
                break;
            }
        }

        if (!accepted) {
            m_progress.status = Status::Learning;
            return false;
        }

        // Commit: drop the tail learned against the old past, then replay the
        // fixed branch with recording ON so the plan really contains the fix.
        int const first = *std::min_element(best.flips.begin(), best.flips.end());
        int const checkpoint = checkpointAtOrBefore(first);
        int const checkpointTick = checkpointTickOf(checkpoint);

        if (hitTick + 1 < static_cast<int>(m_plan.size())) {
            m_plan.resize(hitTick + 1);
        }
        trimCheckpointsAfter(checkpointTick);
        restart(checkpoint, checkpointTick);
        runTo(hitTick + 1, heuristic, &best.flips, true);

        ++m_progress.repairs;
        m_progress.lastFixedTick = first;
        m_progress.deepestRepair =
            std::max(m_progress.deepestRepair, static_cast<int>(best.flips.size()));
        emit(Event { EventKind::RepairAccepted, m_sim.percent(), first,
                     static_cast<int>(best.flips.size()),
                     best.survivedTo - hitTick });

        m_progress.status = Status::Learning;
        return true;
    }

    // "Restart from percent - 2 of where it died": back up, drop the tail,
    // force one random input, keep going. Each repeat backs up further.
    void rewindBeforeHit(double hitPercent, int attempt) {
        double const distance = m_params.rewindPercent * static_cast<double>(attempt + 1);
        double const targetPercent = std::max(hitPercent - distance, 0.0);

        int chosen = 0;
        for (auto const& cp : m_checkpoints) {
            if (cp.percent <= targetPercent) {
                chosen = cp.id;
            }
        }

        int const tick = checkpointTickOf(chosen);
        if (tick == 0) {
            m_plan.clear();
        }
        else if (tick < static_cast<int>(m_plan.size())) {
            m_plan.resize(tick);
        }

        trimCheckpointsAfter(tick);
        restart(chosen, tick);

        int const window = std::min(32, std::max(m_params.maxTicksPerRun - tick, 1));
        int const mutateAt =
            tick + static_cast<int>(nextRandom() % static_cast<std::uint32_t>(window));
        while (static_cast<int>(m_plan.size()) <= mutateAt) {
            m_plan.push_back(Input::Release);
        }
        m_plan[mutateAt] = (nextRandom() & 1u) ? Input::Hold : Input::Release;
    }

    std::uint32_t nextRandom() {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        return m_rng;
    }

    Sim& m_sim;
    Params m_params;
    std::vector<Input> m_plan;
    std::vector<Checkpoint> m_checkpoints;
    EventSink m_sink;
    int m_cursor = 0;
    std::uint32_t m_rng;
    Progress m_progress;
};

} // namespace learnbot
