#pragma once

#include <Geode/Geode.hpp>

#include <vector>

// Reads the level geometry currently loaded around the player.
//
// GD streams objects in ahead of the camera, so PlayLayer::m_objects already
// holds everything within roughly a screen of the player - exactly the horizon
// the heuristic needs. Classifying by GD's own GameObjectType (rather than by a
// hand-written table of object IDs) is what makes this work on custom levels:
// a community orb or pad still reports the right type.
namespace scan {

enum class Kind {
    Hazard,  // spikes, saws
    Solid,   // blocks, slopes
    Orb,     // rings: press while touching
    Pad,     // jump pads: fire on contact, no press needed
    Other,
};

struct Box {
    float left = 0.f;
    float right = 0.f;
    float bottom = 0.f;
    float top = 0.f;
    Kind kind = Kind::Other;

    bool blocks() const { return kind == Kind::Hazard || kind == Kind::Solid; }
};

inline Kind classify(GameObjectType type) {
    switch (type) {
        case GameObjectType::Hazard:
        case GameObjectType::AnimatedHazard:
            return Kind::Hazard;

        case GameObjectType::Solid:
        case GameObjectType::Breakable:
        case GameObjectType::Slope:
            return Kind::Solid;

        // Rings. All of them need a press while the player is inside them;
        // which way they send you is the game's business, not ours.
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
        case GameObjectType::DropRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::TeleportOrb:
        case GameObjectType::CustomRing:
            return Kind::Orb;

        // Pads fire on contact without any input at all.
        case GameObjectType::YellowJumpPad:
        case GameObjectType::PinkJumpPad:
        case GameObjectType::RedJumpPad:
        case GameObjectType::GravityPad:
        case GameObjectType::SpiderPad:
            return Kind::Pad;

        default:
            return Kind::Other;
    }
}

// Everything between `fromX` and `toX` that could kill, block, launch or need a
// press. Portals, decorations and triggers are skipped.
inline std::vector<Box> ahead(PlayLayer* layer, float fromX, float toX) {
    std::vector<Box> out;
    if (!layer || !layer->m_objects) {
        return out;
    }

    auto objects = layer->m_objects;
    unsigned const count = objects->count();
    for (unsigned i = 0; i < count; ++i) {
        auto object = static_cast<GameObject*>(objects->objectAtIndex(i));
        if (!object) {
            continue;
        }

        Kind const kind = classify(object->m_objectType);
        if (kind == Kind::Other) {
            continue;
        }

        auto const rect = object->getObjectRect();
        if (rect.getMaxX() < fromX || rect.getMinX() > toX) {
            continue;
        }

        out.push_back({ rect.getMinX(), rect.getMaxX(), rect.getMinY(), rect.getMaxY(), kind });
    }
    return out;
}

} // namespace scan
