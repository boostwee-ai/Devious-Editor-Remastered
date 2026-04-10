#include "CollabPanel.hpp"
#include "../ColabManager.hpp"

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  CollabPanel
// -----------------------------------------------------------------------
CollabPanel* CollabPanel::create() {
    auto ret = new CollabPanel();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CollabPanel::init() {
    if (!FLAlertLayer::init(150)) return false;

    auto winSize = CCDirector::get()->getWinSize();

    m_mainLayer = CCLayer::create();
    this->addChild(m_mainLayer);

    auto bg = CCScale9Sprite::create("GJ_square01.png");
    bg->setContentSize({320.f, 260.f});
    bg->setPosition(winSize / 2);
    m_mainLayer->addChild(bg);

    auto title = CCLabelBMFont::create("Collaborate", "bigFont.fnt");
    title->setScale(0.7f);
    title->setPosition({winSize.width / 2, winSize.height / 2 + 110.f});
    m_mainLayer->addChild(title);
    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);

    // Position title (FLAlertLayer has a title member sometimes, but let's manual it if needed)
    // For now, let's just use the main setup logic but adjust positioning

    // Empty-state label (shown while list is loading or no peers are found)
    m_emptyLabel = CCLabelBMFont::create(
        "Searching for players\non your local network...",
        "bigFont.fnt"
    );
    m_emptyLabel->setScale(0.4f);
    m_emptyLabel->setOpacity(160);
    m_emptyLabel->setPosition(winSize / 2);
    m_mainLayer->addChild(m_emptyLabel);

    // Scrollable list area
    m_listMenu = CCMenu::create();
    m_listMenu->setPosition(winSize / 2);
    m_listMenu->setContentSize({320.f, 260.f});
    m_listMenu->setLayout(
        ColumnLayout::create()
            ->setAxisReverse(true)
            ->setAutoScale(false)
            ->setGap(4.f)
            ->setAxisAlignment(AxisAlignment::End)
    );
    this->m_mainLayer->addChild(m_listMenu);

    buildList();

    // Refresh every 2 s so the list stays current
    static_cast<CCNode*>(this)->schedule(schedule_selector(CollabPanel::refreshList), 2.f);

    return true;
}

void CollabPanel::buildList() {
    m_listMenu->removeAllChildren();

    auto peers = ColabManager::get()->peers();

    m_emptyLabel->setVisible(peers.empty());
    m_listMenu->setVisible(!peers.empty());

    for (auto& peer : peers) {
        // Only show peers that accept requests
        if (!peer.accepting) continue;

        auto row = CCNode::create();
        row->setContentSize({280.f, 36.f});

        // Name label
        auto lbl = CCLabelBMFont::create(peer.username.c_str(), "bigFont.fnt");
        lbl->setScale(0.45f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({8.f, 18.f});
        row->addChild(lbl);

        // Platform badge
        std::string badge = "[" + peer.platform + "]";
        auto platLbl = CCLabelBMFont::create(badge.c_str(), "chatFont.fnt");
        platLbl->setScale(0.55f);
        platLbl->setOpacity(130);
        platLbl->setAnchorPoint({0.f, 0.5f});
        platLbl->setPosition({8.f + lbl->getScaledContentWidth() + 6.f, 18.f});
        row->addChild(platLbl);

        // Isolated menu for the button
        auto btnMenu = CCMenu::create();
        btnMenu->setPosition({0, 0});
        row->addChild(btnMenu);

        // Invite button
        auto btn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Invite", "bigFont.fnt", "GJ_button_01.png"),
            this,
            menu_selector(CollabPanel::onInvite)
        );
        btn->setPosition({248.f, 18.f});
        btn->setTag(static_cast<int>(ColabManager::get()->peerIndex(peer.ip)));
        btnMenu->addChild(btn);

        m_listMenu->addChild(row);
    }
    m_listMenu->updateLayout();
}

void CollabPanel::refreshList(float) {
    buildList();
}

void CollabPanel::onInvite(CCObject* sender) {
    int idx = sender->getTag();
    auto peers = ColabManager::get()->peers();
    if (idx < 0 || idx >= static_cast<int>(peers.size())) return;

    ColabManager::get()->invitePeer(peers[idx]);
    this->onClose(nullptr);
}

void CollabPanel::onClose(CCObject* o) {
    this->removeFromParentAndCleanup(true);
}

// -----------------------------------------------------------------------
//  InviteRequestPopup
// -----------------------------------------------------------------------
InviteRequestPopup* InviteRequestPopup::create(const std::string& guestName) {
    auto ret = new InviteRequestPopup();
    if (ret->init(guestName)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool InviteRequestPopup::init(std::string guestName) {
    if (!FLAlertLayer::init(150)) return false;
    m_guestName = guestName;

    auto winSize = CCDirector::get()->getWinSize();

    m_mainLayer = CCLayer::create();
    this->addChild(m_mainLayer);

    auto bg = CCScale9Sprite::create("GJ_square01.png");
    bg->setContentSize({300.f, 160.f});
    bg->setPosition(winSize / 2);
    m_mainLayer->addChild(bg);

    std::string title = guestName + " would like\nto collaborate with you";
    auto lbl = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
    lbl->setScale(0.45f);
    lbl->setPosition(winSize / 2 + CCPoint{0.f, 20.f});
    m_mainLayer->addChild(lbl);

    auto menu = CCMenu::create();
    menu->setPosition(winSize / 2 - CCPoint{0.f, 30.f});

    auto yesBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Yes", "bigFont.fnt", "GJ_button_01.png"),
        this, menu_selector(InviteRequestPopup::onAccept)
    );
    yesBtn->setPosition({-60.f, 0.f});
    menu->addChild(yesBtn);

    auto noBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("No", "bigFont.fnt", "GJ_button_06.png"),
        this, menu_selector(InviteRequestPopup::onDecline)
    );
    noBtn->setPosition({60.f, 0.f});
    menu->addChild(noBtn);

    this->m_mainLayer->addChild(menu);
    return true;
}

void InviteRequestPopup::onAccept(CCObject*) {
    ColabManager::get()->acceptInvite();
    this->removeFromParentAndCleanup(true);
}

void InviteRequestPopup::onDecline(CCObject*) {
    ColabManager::get()->declineInvite();
    this->removeFromParentAndCleanup(true);
}
