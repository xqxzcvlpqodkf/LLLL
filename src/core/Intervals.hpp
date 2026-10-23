#pragma once

// Vertical free-space bookkeeping for the flying modes (ship, UFO, wave).
//
// The heuristic asks: "of the vertical space ahead of me, which parts are not
// blocked, and which of those should I aim for?" That is interval subtraction,
// which is easy to get subtly wrong (off-by-one on touching edges, dropping the
// gap you are currently inside), so it lives here as GD-free, tested code.

#include <algorithm>
#include <vector>

namespace learnbot {

struct Interval {
    float lo = 0.f;
    float hi = 0.f;
};

// Removes [blockedLo, blockedHi] from every interval in `free`.
// Intervals that merely touch the blocked range are untouched.
inline void subtractBlocked(std::vector<Interval>& free, float blockedLo, float blockedHi) {
    if (blockedHi < blockedLo) {
        std::swap(blockedLo, blockedHi);
    }

    std::vector<Interval> next;
    next.reserve(free.size() + 1);

    for (auto const& interval : free) {
        bool const overlap = blockedLo < interval.hi && blockedHi > interval.lo;
        if (!overlap) {
            next.push_back(interval);
            continue;
        }
        if (blockedLo > interval.lo) {
            next.push_back({ interval.lo, std::min(blockedLo, interval.hi) });
        }
        if (blockedHi < interval.hi) {
            next.push_back({ std::max(blockedHi, interval.lo), interval.hi });
        }
    }

    free = std::move(next);
}

// Picks the gap to steer for: the one containing `y` if there is one,
// otherwise the one whose centre is nearest. Aims at a fraction `bias` up the
// gap (0 = its floor, 1 = its ceiling) so a ship has room to climb.
inline float chooseTarget(std::vector<Interval> const& free, float y, float bias = 0.45f) {
    if (free.empty()) {
        return y;
    }

    Interval const* best = nullptr;
    float bestScore = 1e30f;

    for (auto const& interval : free) {
        float const centre = (interval.lo + interval.hi) * 0.5f;
        bool const contains = y >= interval.lo && y <= interval.hi;
        float const score = (contains ? 0.f : 1e6f) + std::abs(centre - y);
        if (score < bestScore) {
            bestScore = score;
            best = &interval;
        }
    }

    if (!best) {
        return y;
    }
    return best->lo + (best->hi - best->lo) * bias;
}

} // namespace learnbot
