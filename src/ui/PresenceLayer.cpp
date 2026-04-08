#include "PresenceLayer.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  PresenceIndicator
// -----------------------------------------------------------------------
PresenceIndicator* PresenceIndicator::create(
    const std::string& username, int iconID, int color1, int color2)
{
    auto ret = new PresenceIndicator();
    if (ret->init(username, iconID, color1, color2)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool PresenceIndicator::init(const std::string& username, int iconID, int color1, int color2) {
    if (!CCNode::init()) return false;

    // We size ourselves at a very tall column; actual screen height is set
    // by the caller through setViewportX.
    this->setContentSize({2.f, 4000.f});

    // Vertical white line
    m_line = CCLayerColor::create({255, 255, 255, 180}, 2.f, 4000.f);
    m_line->setPosition({0.f, 0.f});
    this->addChild(m_line, 0);

    // Player icon using Geode's SimplePlayer helper
    auto player = SimplePlayer::create(iconID);
    player->setScale(0.8f);
    player->updatePlayerFrame(iconID, IconType::Cube);
    player->setColor(GameManager::get()->colorForIdx(color1));
    player->setSecondColor(GameManager::get()->colorForIdx(color2));
    m_icon = player;
    m_icon->setPosition({1.f, 200.f});   // midpoint, updated by setViewportX
    this->addChild(m_icon, 1);

    // Name tag
    m_nameTag = CCLabelBMFont::create(username.c_str(), "chatFont.fnt");
    m_nameTag->setScale(0.55f);
    m_nameTag->setOpacity(200);
    m_nameTag->setPosition({1.f, 175.f});
    this->addChild(m_nameTag, 1);

    return true;
}

void PresenceIndicator::setViewportX(float screenX, CCNode* editorLayer) {
    this->setPositionX(screenX);

    // Put icon at vertical center of visible editor viewport
    auto winSize = CCDirector::get()->getWinSize();
    float midY   = winSize.height / 2.f;

    if (editorLayer) {
        // Convert screen mid to local coords
        CCPoint local = editorLayer->convertToNodeSpace({screenX, midY});
        m_icon->setPositionY(local.y);
        m_nameTag->setPositionY(local.y - 22.f);
    }
}

void PresenceIndicator::flashEdit() {
    m_line->stopAllActions();
    m_line->runAction(CCSequence::create(
        CCFadeTo::create(0.05f, 255),
        CCFadeTo::create(0.4f, 180),
        nullptr
    ));
}

// -----------------------------------------------------------------------
//  PresenceLayer
// -----------------------------------------------------------------------
PresenceLayer* PresenceLayer::create() {
    auto ret = new PresenceLayer();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool PresenceLayer::init() {
    if (!CCLayer::init()) return false;
    this->setTouchEnabled(false);
    return true;
}

void PresenceLayer::addPeer(
    const std::string& ip,
    const std::string& username,
    int iconID, int color1, int color2)
{
    if (m_indicators.count(ip)) removePeer(ip);

    auto ind = PresenceIndicator::create(username, iconID, color1, color2);
    if (!ind) return;

    this->addChild(ind, 10);
    m_indicators[ip]  = ind;
    m_worldPos[ip]    = {0.f, 0.f};
}

void PresenceLayer::removePeer(const std::string& ip) {
    auto it = m_indicators.find(ip);
    if (it != m_indicators.end()) {
        it->second->removeFromParent();
        m_indicators.erase(it);
    }
    m_worldPos.erase(ip);
}

void PresenceLayer::updatePeerViewport(
    const std::string& ip, float worldX, float worldY)
{
    m_worldPos[ip] = {worldX, worldY};

    auto it = m_indicators.find(ip);
    if (it == m_indicators.end()) return;

    // Convert world position to screen X using the editor camera.
    // LevelEditorLayer is the parent of this layer — we walk up to it.
    auto editorLayer = this->getParent();
    if (!editorLayer) {
        it->second->setViewportX(worldX, nullptr);
        return;
    }

    CCPoint screen = editorLayer->convertToWorldSpace({worldX, worldY});
    it->second->setViewportX(screen.x, editorLayer);
}

void PresenceLayer::flashPeerEdit(const std::string& ip) {
    auto it = m_indicators.find(ip);
    if (it != m_indicators.end()) {
        it->second->flashEdit();
    }
}

void PresenceLayer::onEditorUpdate(float) {
    // Re-project all world positions to screen space each frame
    // so the indicators follow the camera.
    for (auto& [ip, worldPos] : m_worldPos) {
        updatePeerViewport(ip, worldPos.x, worldPos.y);
    }
}
