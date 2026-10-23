#pragma once

// The difficulty gate: which levels the bot is allowed to run on.
//
// Kept free of GD types (it takes plain ints) so the rule can be unit tested.
// The values match GD's own:
//   GJGameLevel::m_demon            0 = not a demon, non-zero = demon
//   GJGameLevel::m_demonDifficulty  HardDemon = 0, EasyDemon = 3,
//                                   MediumDemon = 4, InsaneDemon = 5,
//                                   ExtremeDemon = 6
//   (there is no "normal demon" tier in the game)

namespace learnbot {

struct LevelInfo {
    int demon = 0;            // m_demon
    int demonDifficulty = 0;  // only meaningful when demon != 0
    int stars = 0;            // m_stars
    int normalPercent = 0;    // m_normalPercent
};

struct GateSettings {
    // The rule the mod ships with: medium, insane and extreme demons are never
    // touched. Easy and hard demons are allowed.
    bool blockEasyDemon = false;
    bool blockHardDemon = false;
    bool blockMediumDemon = true;
    bool blockInsaneDemon = true;
    bool blockExtremeDemon = true;

    // Optional extras, all off by default.
    bool blockCompletedLevels = false;  // don't re-clear your own 100%s
};

enum class GateReason {
    Allowed,
    BlockedDemonTier,
    BlockedAlreadyComplete,
    UnknownTier,  // a demon whose tier we cannot read: refuse rather than guess
};

struct GateResult {
    bool allowed = true;
    GateReason reason = GateReason::Allowed;
    int tier = -1;  // m_demonDifficulty, for the HUD
};

// Demon difficulty values, spelled out so the caller does not need GD enums.
inline constexpr int kDemonHard = 0;
inline constexpr int kDemonEasy = 3;
inline constexpr int kDemonMedium = 4;
inline constexpr int kDemonInsane = 5;
inline constexpr int kDemonExtreme = 6;

inline GateResult evaluate(LevelInfo const& level, GateSettings const& settings) {
    GateResult result;

    bool const isDemon = level.demon != 0;

    if (isDemon) {
        result.tier = level.demonDifficulty;

        bool blocked = false;
        switch (level.demonDifficulty) {
            case kDemonEasy:    blocked = settings.blockEasyDemon; break;
            case kDemonHard:    blocked = settings.blockHardDemon; break;
            case kDemonMedium:  blocked = settings.blockMediumDemon; break;
            case kDemonInsane:  blocked = settings.blockInsaneDemon; break;
            case kDemonExtreme: blocked = settings.blockExtremeDemon; break;
            default:
                // Some other value entirely (unrated demon, future update).
                // Refuse: silently guessing "allowed" is how a bot ends up on
                // an extreme demon.
                result.allowed = false;
                result.reason = GateReason::UnknownTier;
                return result;
        }

        if (blocked) {
            result.allowed = false;
            result.reason = GateReason::BlockedDemonTier;
            return result;
        }
    }

    if (settings.blockCompletedLevels && level.normalPercent >= 100) {
        result.allowed = false;
        result.reason = GateReason::BlockedAlreadyComplete;
        return result;
    }

    return result;
}

// Human readable tier name, for the HUD and for the log.
inline char const* tierName(int tier) {
    switch (tier) {
        case kDemonEasy: return "Easy Demon";
        case kDemonHard: return "Hard Demon";
        case kDemonMedium: return "Medium Demon";
        case kDemonInsane: return "Insane Demon";
        case kDemonExtreme: return "Extreme Demon";
        default: return "Demon";
    }
}

inline char const* reasonText(GateResult const& result) {
    switch (result.reason) {
        case GateReason::Allowed: return "allowed";
        case GateReason::BlockedDemonTier: return tierName(result.tier);
        case GateReason::BlockedAlreadyComplete: return "already completed";
        case GateReason::UnknownTier: return "unknown demon tier";
    }
    return "?";
}

} // namespace learnbot
