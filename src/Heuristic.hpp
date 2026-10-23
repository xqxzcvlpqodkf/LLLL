#pragma once

#include <Geode/Geode.hpp>

#include "Scan.hpp"
#include "State.hpp"
#include "core/Intervals.hpp"
#include "core/Learner.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// The per-tick "what would a decent player do here" rule.
//
// The learner only needs this where it has no learned input yet, so it can stay
// simple - but it decides how much the search has to fix, so it knows about the
// things that make GD gameplay what it is:
//
//   spikes and blocks  → jump window ahead
//   pits / gaps        → jump before the ground runs out
//   orbs (rings)       → press while touching one
//   pads               → fire on their own: do NOT jump into them
//   gravity portals    → assumed to be handled by the game, not by us
//
// Flying modes (ship, UFO, wave, swing) steer towards the middle of the largest
// free vertical gap instead.
namespace heur {

inline bool isFlying(PlayerObject* player) {
    return player->m_isShip || player->m_isBird || player->m_isDart || player->m_isSwing;
}

inline bool isHoldJump(PlayerObject* player) {
    // Robot jump height depends on how long the button is held, so it wants the
    // same rule as the cube but with a longer hold: handled by returning Hold
    // across the whole window.
    return player->m_isRobot;
}

// Distance to the end of the ground we are standing on, if there is a gap in
// the next `lookahead` units. Returns a huge number when the ground continues.
//
// Works purely from what is loaded: if there is no solid block under or ahead
// of the player within reach, treat the gap as starting at the end of the last
// solid we can see.
inline float distanceToPitEnd(PlayLayer* layer, std::vector<scan::Box> const& boxes, float playerRight) {
    float const lookahead = 220.f;
    float lastSolid = playerRight;

    for (auto const& box : boxes) {
        if (box.kind != scan::Kind::Solid) {
            continue;
        }
        if (box.left <= playerRight && box.right >= playerRight) {
            lastSolid = box.right;  // we are standing on this one
        }
        else if (box.left > playerRight && box.left < playerRight + lookahead) {
            if (box.left - lastSolid > 60.f) {
                return lastSolid - playerRight;  // a real gap before the next block
            }
            lastSolid = std::max(lastSolid, box.right);
        }
    }

    (void)layer;
    return 1e9f;
}

inline learnbot::Input decide(PlayLayer* layer, PlayerObject* player, BotSettings const& settings) {
    if (!layer || !player) {
        return learnbot::Input::Release;
    }

    auto const pos = player->getPosition();
    float const halfHull = settings.hullWidth * 0.5f;

    // ---- flying ---------------------------------------------------------
    if (isFlying(player)) {
        float const fromX = pos.x + halfHull;
        float const toX = pos.x + std::max(settings.flyingLookahead, 60.f);

        float const range = 420.f;
        std::vector<learnbot::Interval> free { { pos.y - range, pos.y + range } };
        for (auto const& box : scan::ahead(layer, fromX, toX)) {
            if (box.blocks()) {
                learnbot::subtractBlocked(free, box.bottom, box.top);
            }
        }
        if (free.empty()) {
            return learnbot::Input::Release;
        }

        float const target = learnbot::chooseTarget(free, pos.y, 0.45f);
        float const deadzone = player->m_isDart ? 0.5f : 6.f;

        bool hold = pos.y < target - deadzone;
        if (player->m_isUpsideDown) {
            hold = pos.y > target + deadzone;
        }
        return hold ? learnbot::Input::Hold : learnbot::Input::Release;
    }

    // ---- ground modes ---------------------------------------------------
    float const horizon = std::max(settings.jumpClearanceMax + 40.f, 140.f);
    auto boxes = scan::ahead(layer, pos.x, pos.x + horizon);

    // An orb is live if the player is inside its circle. Rings work in mid-air,
    // so this is checked before anything about being on the ground.
    float const orbReach = settings.orbReach;
    for (auto const& box : boxes) {
        if (box.kind != scan::Kind::Orb) {
            continue;
        }
        float const cx = (box.left + box.right) * 0.5f;
        float const cy = (box.bottom + box.top) * 0.5f;
        float const dx = cx - pos.x;
        float const dy = cy - pos.y;
        if (std::abs(dx) <= orbReach && std::abs(dy) <= orbReach) {
            return learnbot::Input::Hold;  // press it
        }
    }

    if (!player->m_isOnGround) {
        return learnbot::Input::Release;
    }

    float const playerRight = pos.x + halfHull;

    // A pad about to fire us: do not jump into it, let it do the work.
    for (auto const& box : boxes) {
        if (box.kind != scan::Kind::Pad) {
            continue;
        }
        float const distance = box.left - playerRight;
        if (distance > -20.f && distance < settings.padAvoidDistance) {
            return learnbot::Input::Release;
        }
    }

    float nearestHazard = 1e9f;
    for (auto const& box : boxes) {
        if (box.kind != scan::Kind::Hazard) {
            continue;
        }
        float const distance = box.left - playerRight;
        if (distance < -halfHull) {
            continue;  // behind us
        }
        nearestHazard = std::min(nearestHazard, distance);
    }

    // The ground running out ahead counts exactly like a hazard: jump it.
    float const pit = distanceToPitEnd(layer, boxes, playerRight);
    if (pit < settings.pitJumpDistance) {
        nearestHazard = std::min(nearestHazard, pit);
    }

    bool const inWindow = nearestHazard >= settings.jumpClearanceMin
        && nearestHazard <= settings.jumpClearanceMax;
    return inWindow ? learnbot::Input::Hold : learnbot::Input::Release;
}

} // namespace heur
