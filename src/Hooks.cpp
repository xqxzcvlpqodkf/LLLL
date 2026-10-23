#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Bot.hpp"
#include "State.hpp"

using namespace geode::prelude;

namespace {

// One HUD label per PlayLayer, updated only when the text changes.
CCLabelBMFont* hudLabel(PlayLayer* layer, bool create) {
    if (auto existing = layer->getChildByID("learnbot-hud"_spr)) {
        return static_cast<CCLabelBMFont*>(existing);
    }
    if (!create) {
        return nullptr;
    }

    auto label = CCLabelBMFont::create("LearnBot", "bigFont.fnt");
    if (!label) {
        return nullptr;
    }
    auto const winSize = CCDirector::sharedDirector()->getWinSize();
    label->setScale(.35f);
    label->setAnchorPoint({ 0.f, 1.f });
    label->setPosition({ 8.f, winSize.height - 26.f });
    label->setOpacity(200);
    label->setID("learnbot-hud"_spr);
    layer->addChild(label, 1000);
    return label;
}

} // namespace

// ---------------------------------------------------------------------------
// PlayLayer: gate, death interception, safe mode, HUD
// ---------------------------------------------------------------------------
class $modify(LearnBotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }
        LearnBot::get().onLevelStart(this);
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (LearnBot::get().onDeath(player, object)) {
            // The bot swallowed this death: either it is mid learning pass and
            // needs to observe it, or the noclip fallback is on.
            return;
        }

        // Anything the bot touches must never save progress, so the attempt is
        // marked as a test run - the same flag GD uses for start-pos runs.
        if (g_bot.enabled && g_bot.safeMode) {
            bool const wasTestMode = m_isTestMode;
            m_isTestMode = true;
            PlayLayer::destroyPlayer(player, object);
            m_isTestMode = wasTestMode;
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        if (g_bot.enabled && g_bot.safeMode) {
            bool const wasTestMode = m_isTestMode;
            m_isTestMode = true;
            PlayLayer::levelComplete();
            m_isTestMode = wasTestMode;
            return;
        }
        PlayLayer::levelComplete();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        LearnBot::get().onReset();
    }

    void onExit() {
        LearnBot::get().onLevelExit();
        PlayLayer::onExit();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!g_bot.showHud) {
            if (auto existing = this->getChildByID("learnbot-hud"_spr)) {
                existing->setVisible(false);
            }
            return;
        }

        auto label = hudLabel(this, true);
        if (!label) {
            return;
        }

        auto const status = LearnBot::get().statusText();
        if (status != LearnBot::get().lastHudText()) {
            label->setString(status.c_str());
            label->setVisible(true);
            label->setColor(
                LearnBot::get().mode() == LearnBot::Mode::Blocked
                    ? ccColor3B { 255, 90, 90 }
                    : ccColor3B { 190, 255, 170 }
            );
            LearnBot::get().rememberHudText(status);
        }
    }
};

// ---------------------------------------------------------------------------
// GJBaseGameLayer: the tick pump
// ---------------------------------------------------------------------------
class $modify(LearnBotTick, GJBaseGameLayer) {
    void update(float dt) {
        auto& bot = LearnBot::get();

        // PlayLayer and LevelEditorLayer both derive from GJBaseGameLayer;
        // m_isEditor is the reliable way to tell them apart.
        if (!m_isEditor) {
            GameSim::Advance advance = [this](float step) {
                // Calls the original, not this hook.
                GJBaseGameLayer::update(step);
            };

            if (bot.beforeUpdate(this, std::move(advance))) {
                // The learning pass owned this frame: it advanced the game
                // itself, thousands of ticks at a time.
                return;
            }
        }

        GJBaseGameLayer::update(dt);
        bot.afterUpdate(this);
    }
};

// ---------------------------------------------------------------------------
// Pause menu: a button that arms/disarms the bot
// ---------------------------------------------------------------------------
class $modify(LearnBotPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = this->getChildByID("right-button-menu");
        if (!menu) {
            menu = this->getChildByID("left-button-menu");
        }
        if (!menu) {
            return;
        }

        auto sprite = ButtonSprite::create("LearnBot");
        sprite->setScale(.55f);

        auto button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(LearnBotPauseLayer::onToggleBot)
        );
        button->setID("learnbot-toggle-button"_spr);
        menu->addChild(button);
        menu->updateLayout();
    }

    void onToggleBot(CCObject*) {
        bool const enabled = !Mod::get()->getSettingValue<bool>("enabled");
        Mod::get()->setSettingValue<bool>("enabled", enabled);
        Notification::create(
            enabled ? "LearnBot armed - restart the level" : "LearnBot disarmed",
            NotificationIcon::Info
        )->show();
    }
};
