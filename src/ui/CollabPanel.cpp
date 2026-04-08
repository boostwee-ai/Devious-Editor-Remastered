#include "CollabPanel.hpp"
#include "../ColabManager.hpp"

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  CollabPanel
// -----------------------------------------------------------------------
CollabPanel* CollabPanel::create() {
    auto ret = new CollabPanel();
    if (ret->initAnchored(320.f, 260.f)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CollabPanel::setup() {
    this->setTitle("Collaborate");

    // Empty-state label (shown while list is loading or no peers are found)
    m_emptyLabel = CCLabelBMFont::create(
        "Searching for players\non your local network...",
        "bigFont.fnt"
    );
    m_emptyLabel->setScale(0.4f);
    m_emptyLabel->setOpacity(160);
    m_emptyLabel->setPosition(m_mainLayer->getContentSize() / 2);
    m_mainLayer->addChild(m_emptyLabel);

    // Scrollable list area
    m_listMenu = CCMenu::create();
    m_listMenu->setPosition({0, 0});
    m_listMenu->setContentSize(m_mainLayer->getContentSize());
    m_listMenu->setLayout(
        ColumnLayout::create()
            ->setAxisReverse(true)
            ->setAutoScale(false)
            ->setGap(4.f)
            ->setAxisAlignment(AxisAlignment::End)
    );
    m_mainLayer->addChild(m_listMenu);

    buildList();

    // Refresh every 2 s so the list stays current
    this->schedule(schedule_selector(CollabPanel::refreshList), 2.f);

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

        auto row = CCMenu::create();
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

        // Invite button
        auto btn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Invite", "bigFont.fnt", "GJ_button_01.png"),
            this,
            menu_selector(CollabPanel::onInvite)
        );
        btn->setPosition({248.f, 18.f});
        btn->setTag(static_cast<int>(ColabManager::get()->peerIndex(peer.ip)));
        row->addChild(btn);

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
    Popup::onClose(o);
}

// -----------------------------------------------------------------------
//  InviteRequestPopup
// -----------------------------------------------------------------------
InviteRequestPopup* InviteRequestPopup::create(const std::string& guestName) {
    auto ret = new InviteRequestPopup();
    if (ret->initAnchored(300.f, 160.f, guestName)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool InviteRequestPopup::setup(std::string guestName) {
    m_guestName = guestName;

    std::string title = guestName + " would like\nto collaborate with you";
    auto lbl = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
    lbl->setScale(0.45f);
    lbl->setPosition(m_mainLayer->getContentSize() / 2 + CCPoint{0.f, 20.f});
    m_mainLayer->addChild(lbl);

    auto menu = CCMenu::create();
    menu->setPosition(m_mainLayer->getContentSize() / 2 - CCPoint{0.f, 30.f});

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

    m_mainLayer->addChild(menu);
    return true;
}

void InviteRequestPopup::onAccept(CCObject*) {
    ColabManager::get()->acceptInvite();
    this->onClose(nullptr);
}

void InviteRequestPopup::onDecline(CCObject*) {
    ColabManager::get()->declineInvite();
    this->onClose(nullptr);
}
