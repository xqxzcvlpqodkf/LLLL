// Tests for the LearnBot core. Build & run:
//
//   ./run-tests.sh
//   (or: g++ -std=c++23 -O2 -o build/learner_test tests/learner_test.cpp && ./build/learner_test)
//
// Nothing here touches Geometry Dash. The same learner code runs against
// tests/ToyWorld.hpp, a 240 Hz toy platformer with spikes, pits and orbs.

#include "ToyWorld.hpp"

#include "../src/core/Gate.hpp"
#include "../src/core/Intervals.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace learnbot;
using toy::ToySim;

namespace {

int g_failures = 0;

void check(bool condition, std::string const& what) {
    if (condition) {
        std::printf("    ok    %s\n", what.c_str());
    }
    else {
        std::printf("    FAIL  %s\n", what.c_str());
        ++g_failures;
    }
}

const char* statusName(Status status) {
    switch (status) {
        case Status::Idle: return "idle";
        case Status::Learning: return "learning";
        case Status::Repairing: return "repairing";
        case Status::Complete: return "complete";
        case Status::Stuck: return "stuck";
    }
    return "?";
}

void printProgress(Progress const& p) {
    std::printf("    status=%s best=%.1f%% deaths=%d repairs=%d rewinds=%d deepest=%d sims=%lld\n",
        statusName(p.status), p.best, p.deaths, p.repairs, p.rewinds,
        p.deepestRepair, p.simulatedTicks);
}

// A naive player: jump only once the spike is almost touching. Always too late.
struct NaiveHeuristic {
    ToySim& sim;
    Input operator()() const {
        if (!sim.onGround()) {
            return Input::Release;
        }
        double const d = sim.distanceToNextSpike();
        return (d < 12.0 && d > -4.0) ? Input::Hold : Input::Release;
    }
};

// The same player, but it also knows orbs and pits exist: press while touching
// an orb, and jump when the ground is about to run out.
struct OrbAwareHeuristic {
    ToySim& sim;
    Input operator()() const {
        if (sim.distanceToNextOrb() < 30.0) {
            return Input::Hold;  // orbs work in mid-air
        }
        if (!sim.onGround()) {
            return Input::Release;
        }
        double const pit = sim.distanceToNextPit();
        if (pit < 50.0 && pit > 10.0) {
            return Input::Hold;
        }
        double const d = sim.distanceToNextSpike();
        return (d < 12.0 && d > -4.0) ? Input::Hold : Input::Release;
    }
};

Params defaultParams() {
    Params params;
    params.searchBack = 32;
    params.lookaheadTicks = 120;
    params.checkpointEvery = 60;
    params.budgetTicks = 2'000'000;
    params.maxCandidates = 96;
    return params;
}

void testSanity() {
    std::printf("[sanity] a decent heuristic can clear the toy course\n");
    auto course = toy::makeCourse(110.0, 8);
    ToySim sim(course);
    int deaths = 0;
    for (int i = 0; i < 20000; ++i) {
        double const d = sim.distanceToNextSpike();
        Input const in = (sim.onGround() && d < 54.0 && d > 20.0) ? Input::Hold : Input::Release;
        auto const r = sim.step(in);
        if (r == StepResult::Died) { ++deaths; break; }
        if (r == StepResult::Completed) { break; }
    }
    check(deaths == 0, "the course is passable without the learner");
}

void testLearnerFixesNaivePlay() {
    std::printf("[learn] naive heuristic + learner clears the course\n");
    ToySim sim(toy::makeCourse(110.0, 8));
    Learner learner(sim, defaultParams());
    NaiveHeuristic heuristic { sim };
    auto const progress = learner.run(heuristic);
    printProgress(progress);

    check(progress.status == Status::Complete, "the learner completed the level");
    check(progress.deaths > 0, "it actually had to fix something");
    check(progress.best > 99.0, "it reached 100%");
    check(progress.deepestRepair >= 1, "at least one input was changed");
}

void testOrbSection() {
    std::printf("[orbs] a pit that needs a mid-air orb press\n");

    // A naive player with no idea orbs exist falls in.
    {
        ToySim sim(toy::makeOrbCourse());
        NaiveHeuristic heuristic { sim };
        bool completed = false;
        for (int i = 0; i < 20000; ++i) {
            auto const r = sim.step(heuristic());
            if (r == StepResult::Died) break;
            if (r == StepResult::Completed) { completed = true; break; }
        }
        check(!completed, "without orb knowledge, the pit cannot be crossed");
    }

    // With the orb rule, the heuristic starts in the right place and the
    // learner only has to refine the timing.
    ToySim sim(toy::makeOrbCourse());
    Params params = defaultParams();
    params.searchBack = 40;
    params.maxCandidates = 160;
    Learner learner(sim, params);
    OrbAwareHeuristic heuristic { sim };
    auto const progress = learner.run(heuristic);
    printProgress(progress);

    check(progress.status == Status::Complete, "orb-aware play clears the orb section");
    check(progress.best > 99.0, "it reached 100%");
}

void testBadPlanIsRepaired() {
    std::printf("[learn] a garbage preloaded plan still gets repaired\n");
    ToySim sim(toy::makeCourse(110.0, 6));

    std::vector<Input> badPlan(400, Input::Hold);

    Params params = defaultParams();
    params.searchBack = 48;
    params.maxRewindsPerDeath = 40;
    params.budgetTicks = 4'000'000;

    Learner learner(sim, params, badPlan);
    NaiveHeuristic heuristic { sim };
    auto const progress = learner.run(heuristic);
    printProgress(progress);

    check(progress.status == Status::Complete, "the learner recovered from the bad plan");
    check(progress.repairs + progress.rewinds > 0, "it repaired and/or rewound at least once");
}

void testDeepSearch() {
    std::printf("[depth] two inputs may change at once\n");
    ToySim sim(toy::makeCourse(110.0, 6, 3));

    Params params = defaultParams();
    params.maxDepth = 2;
    params.maxCandidates = 200;
    params.searchBack = 48;

    Learner learner(sim, params);
    NaiveHeuristic heuristic { sim };
    auto const progress = learner.run(heuristic);
    printProgress(progress);

    check(progress.status == Status::Complete, "depth-2 search completes the tight course");
    check(progress.deepestRepair <= 2, "it never changes more inputs than allowed");
}

void testDepthOneStillWorks() {
    std::printf("[depth] depth 1 is still usable (cheap mode)\n");
    ToySim sim(toy::makeCourse(110.0, 5));

    Params params = defaultParams();
    params.maxDepth = 1;

    Learner learner(sim, params);
    NaiveHeuristic heuristic { sim };
    auto const progress = learner.run(heuristic);
    printProgress(progress);

    check(progress.deepestRepair <= 1, "only single-input fixes are used");
    check(progress.status == Status::Complete, "it still completes the simple course");
}

void testBudget() {
    std::printf("[budget] a tiny budget stops the pass instead of hanging\n");
    ToySim sim(toy::makeCourse(110.0, 10));

    Params params = defaultParams();
    params.budgetTicks = 500;  // about two seconds of simulated gameplay

    Learner learner(sim, params);
    NaiveHeuristic heuristic { sim };
    auto const progress = learner.run(heuristic);
    printProgress(progress);

    check(progress.status == Status::Stuck, "it stops when the budget runs out");
    check(progress.outOfBudget || progress.simulatedTicks >= params.budgetTicks,
          "the tick budget is what stopped it");
    check(progress.simulatedTicks < params.budgetTicks + 400,
          "it does not blow far past the budget");
}

void testEvents() {
    std::printf("[events] the pass reports what it is doing\n");
    ToySim sim(toy::makeCourse(110.0, 4));

    std::vector<EventKind> seen;
    Params params = defaultParams();
    Learner learner(sim, params, {}, [&seen](Event const& event) { seen.push_back(event.kind); });
    NaiveHeuristic heuristic { sim };
    learner.run(heuristic);

    auto contains = [&seen](EventKind kind) {
        return std::find(seen.begin(), seen.end(), kind) != seen.end();
    };

    check(contains(EventKind::PassStarted), "it announces the start");
    check(contains(EventKind::Hit), "it reports hits");
    check(contains(EventKind::RepairAccepted), "it reports accepted fixes");
    check(contains(EventKind::Completed), "it reports completion");
}

void testDeterminism() {
    std::printf("[determinism] same seed, same result\n");
    auto runOnce = [](std::uint32_t seed) {
        ToySim sim(toy::makeCourse(110.0, 5));
        Params params = defaultParams();
        params.seed = seed;
        Learner learner(sim, params);
        NaiveHeuristic heuristic { sim };
        auto const p = learner.run(heuristic);
        return std::make_pair(p.status, learner.plan().size());
    };

    auto const a = runOnce(12345u);
    auto const b = runOnce(12345u);
    check(a == b, "identical seeds give identical plans");
}

void testGate() {
    std::printf("[gate] the shipped rule: no medium/insane/extreme demons\n");
    GateSettings const settings {};

    auto allowed = [&](int demon, int tier) {
        return evaluate(LevelInfo { demon, tier, 0, 0 }, settings).allowed;
    };

    check(allowed(0, 0), "a non-demon level is allowed");
    check(allowed(0, 4), "stale demonDifficulty on a non-demon is ignored");
    check(allowed(1, kDemonEasy), "easy demon is allowed");
    check(allowed(1, kDemonHard), "hard demon is allowed");
    check(!allowed(1, kDemonMedium), "medium demon is blocked");
    check(!allowed(1, kDemonInsane), "insane demon is blocked");
    check(!allowed(1, kDemonExtreme), "extreme demon is blocked");
    check(!allowed(1, 99), "an unreadable demon tier is refused, not guessed");

    auto blocked = evaluate(LevelInfo { 1, kDemonExtreme, 0, 0 }, settings);
    check(std::string(reasonText(blocked)) == "Extreme Demon", "reason reads 'Extreme Demon'");

    GateSettings strict;
    strict.blockEasyDemon = true;
    strict.blockHardDemon = true;
    check(!evaluate(LevelInfo { 1, kDemonEasy, 0, 0 }, strict).allowed, "easy demons blockable");
    check(evaluate(LevelInfo { 0, 0, 0, 0 }, strict).allowed, "non-demons still allowed when strict");
}

void testIntervals() {
    std::printf("[flying] free-gap maths for ship/wave steering\n");
    using learnbot::Interval;

    std::vector<Interval> free { { 0.f, 100.f } };
    subtractBlocked(free, 40.f, 60.f);
    check(free.size() == 2, "a block in the middle splits one gap into two");

    std::vector<Interval> touching { { 0.f, 40.f } };
    subtractBlocked(touching, 40.f, 80.f);
    check(touching.size() == 1 && touching[0].hi == 40.f, "a touching block changes nothing");

    std::vector<Interval> covering { { 10.f, 20.f } };
    subtractBlocked(covering, 0.f, 100.f);
    check(covering.empty(), "a block covering everything leaves no gap");

    std::vector<Interval> gaps { { 0.f, 30.f }, { 100.f, 200.f } };
    check(chooseTarget(gaps, 120.f) > 100.f, "aims into the gap holding the player");
    check(chooseTarget(gaps, 60.f) < 30.f, "picks the nearest gap when in neither");
    check(chooseTarget({}, 55.f) == 55.f, "no gaps means hold height");
}

} // namespace

int main() {
    std::printf("LearnBot core tests\n\n");
    testSanity();
    testLearnerFixesNaivePlay();
    testOrbSection();
    testBadPlanIsRepaired();
    testDeepSearch();
    testDepthOneStillWorks();
    testBudget();
    testEvents();
    testDeterminism();
    testGate();
    testIntervals();

    std::printf("\n%s (%d failure%s)\n",
        g_failures == 0 ? "PASS" : "FAIL",
        g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
