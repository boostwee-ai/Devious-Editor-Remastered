#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include "../network/Discovery.hpp"
#include "../network/Session.hpp"

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  CollabPanel — popup listing available collaborators on the LAN.
//  Displayed when the user clicks the collab button in the editor toolbar.
// -----------------------------------------------------------------------
class CollabPanel : public FLAlertLayer {
public:
    static CollabPanel* create();

protected:
    bool init() override;
    void onClose(CCObject*) override;

private:
    void buildList();
    void onInvite(CCObject* sender);
    void refreshList(float dt);

    CCLayer*    m_mainLayer = nullptr;
    CCMenu*     m_listMenu = nullptr;
    CCLabelBMFont* m_emptyLabel = nullptr;
};

// -----------------------------------------------------------------------
//  InviteRequestPopup — shown on the host side when a guest requests collab.
// -----------------------------------------------------------------------
class InviteRequestPopup : public FLAlertLayer {
public:
    static InviteRequestPopup* create(const std::string& guestName);

protected:
    bool init(std::string guestName);

private:
    void onAccept(CCObject*);
    void onDecline(CCObject*);

    CCLayer*    m_mainLayer = nullptr;
    std::string m_guestName;
};
