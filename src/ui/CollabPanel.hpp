#pragma once
#include <Geode/Geode.hpp>

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  CollabPanel — popup listing available collaborators on the LAN.
// -----------------------------------------------------------------------
class CollabPanel : public geode::Popup<> {
public:
    static CollabPanel* create();

protected:
    bool setup() override;
    void onClose(CCObject*) override;

private:
    void buildList();
    void onInvite(CCObject* sender);
    void refreshList(float dt);

    CCMenu*        m_listMenu   = nullptr;
    CCLabelBMFont* m_emptyLabel = nullptr;
};

// -----------------------------------------------------------------------
//  InviteRequestPopup — shown on the host when a guest requests collab.
// -----------------------------------------------------------------------
class InviteRequestPopup : public geode::Popup<std::string> {
public:
    static InviteRequestPopup* create(const std::string& guestName);

protected:
    bool setup(std::string guestName) override;

private:
    void onAccept(CCObject*);
    void onDecline(CCObject*);

    std::string m_guestName;
};
