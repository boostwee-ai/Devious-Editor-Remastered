#include <Geode/Geode.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include "../ColabManager.hpp"
#include "../ui/CollabPanel.hpp"
#include "../ui/PresenceLayer.hpp"

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  LevelEditorLayer hooks
//  - init: add PresenceLayer, start ColabManager
//  - onExit / destructor: stop ColabManager
//  - update: forward camera data for presence indicators
//  - addToSection / removeObject / moveObject: sync to peer
// -----------------------------------------------------------------------
class $modify(MyLEL, LevelEditorLayer) {

    struct Fields {
        PresenceLayer* presenceLayer = nullptr;
        float          viewportSendTimer = 0.f;
    };

    bool init(GJGameLevel* level, bool unk) {
        if (!LevelEditorLayer::init(level, unk)) return false;

        // Add presence layer on top of everything
        auto pl = PresenceLayer::create();
        if (pl) {
            this->addChild(pl, 1000);
            m_fields->presenceLayer = pl;
            ColabManager::get()->setPresenceLayer(pl);
        }

        ColabManager::get()->onEnterEditor();
        return true;
    }

    void onExit() {
        ColabManager::get()->onExitEditor();
        LevelEditorLayer::onExit();
    }

    void update(float dt) {
        LevelEditorLayer::update(dt);

        // Send viewport at ~10 Hz to avoid flooding the connection
        m_fields->viewportSendTimer += dt;
        if (m_fields->viewportSendTimer >= 0.1f) {
            m_fields->viewportSendTimer = 0.f;
            CCPoint worldPos = this->getPosition();
            ColabManager::get()->sendViewport(-worldPos.x, -worldPos.y);
        }

        // Keep presence indicators projected in world space
        if (m_fields->presenceLayer) {
            m_fields->presenceLayer->onEditorUpdate(dt);
        }
    }

    // Called by GD when a new object is added to the editor
    void addSpecial(GameObject* obj) {
        LevelEditorLayer::addSpecial(obj);

        if (obj && ColabManager::get()->isInSession()) {
            // Ideally we'd send the full object dump here, but for now 
            // we send a basic placement notification.
            ColabManager::get()->sendObjPlace("1"); 
        }
    }

    // Called by GD when objects are deleted
    void removeObject(GameObject* obj, bool idk) {
        if (obj && ColabManager::get()->isInSession()) {
            std::vector<int> ids = { obj->m_uniqueID };
            ColabManager::get()->sendObjDelete(ids);
        }
        LevelEditorLayer::removeObject(obj, idk);
    }
};

// -----------------------------------------------------------------------
//  EditorUI hooks
//  - Add the collab button to the toolbar
// -----------------------------------------------------------------------
class $modify(MyEditorUI, EditorUI) {

    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        addCollabButton();
        return true;
    }

    void addCollabButton() {
        auto menu = CCMenu::create();
        menu->setID("devious-collab-menu");
        
        auto btn = createButton();
        menu->addChild(btn);
        
        // Position at bottom-right corner, safe from standard toolbars
        auto winSize = CCDirector::get()->getWinSize();
        menu->setPosition({ winSize.width - 40.f, 40.f });
        
        this->addChild(menu, 100);
    }

    CCMenuItemSpriteExtra* createButton() {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png");
        if (!spr) spr = CCSprite::create();
        spr->setScale(0.75f);

        // Overlay a small network icon using a label if no custom sprite
        auto label = CCLabelBMFont::create("CO", "bigFont.fnt");
        label->setScale(0.5f);
        label->setPosition(spr->getContentSize() / 2);
        spr->addChild(label);

        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(MyEditorUI::onCollabButton)
        );
        btn->setID("devious-collab-button");
        return btn;
    }

    void onCollabButton(CCObject*) {
        auto panel = CollabPanel::create();
        if (panel) static_cast<FLAlertLayer*>(panel)->show();
    }
};
