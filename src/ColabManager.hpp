#pragma once
#include <Geode/Geode.hpp>
#include "network/Discovery.hpp"
#include "network/Session.hpp"
#include "ui/PresenceLayer.hpp"

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  ColabManager — singleton that owns networking state and coordinates
//  between the editor hooks, discovery, and session.
// -----------------------------------------------------------------------
class ColabManager {
public:
    static ColabManager* get();

    // ---- Lifecycle ----
    // Called when the user enters the editor.
    void onEnterEditor();
    // Called when the user exits the editor.
    void onExitEditor();

    // ---- Peer list ----
    std::vector<PeerInfo> peers() const;
    int peerIndex(const std::string& ip) const;  // returns -1 if not found

    // ---- Guest: invite flow ----
    void invitePeer(const PeerInfo& peer);

    // ---- Host: accept/decline flow ----
    void acceptInvite();
    void declineInvite();
    // Internal: called when the session layer gets a ColabReq (host only)
    void onIncomingInvite(const std::string& guestName);

    // ---- Session sync ----
    // Send viewport position to peer (called from editor update).
    void sendViewport(float worldX, float worldY);
    // Send object placement (GD object string format).
    void sendObjPlace(const std::string& objStr);
    // Send object deletion by UID list.
    void sendObjDelete(const std::vector<int>& ids);
    // Send object edit payload.
    void sendObjEdit(int id, const std::string& propJson);

    // ---- PresenceLayer management ----
    void setPresenceLayer(PresenceLayer* layer);
    PresenceLayer* presenceLayer() const { return m_presenceLayer; }

    // ---- State queries ----
    bool isInSession() const;
    bool isHost() const { return m_isHost; }

private:
    ColabManager() = default;

    void startDiscovery();
    void stopDiscovery();

    void onFrame(const Frame& f);
    void onSessionError(const std::string& msg);
    void onSessionConnected();

    void handleViewport(const std::string& body);
    void handleObjPlace(const std::string& body);
    void handleObjDelete(const std::string& body);
    void handleObjEdit(const std::string& body);
    void flashRemotePeer();

    // Dispatch to Cocos main thread.
    template<typename F>
    void mainThread(F&& fn);

    Discovery   m_discovery;
    std::unique_ptr<Session> m_session;

    PresenceLayer* m_presenceLayer = nullptr;

    bool    m_isHost    = false;
    bool    m_inEditor  = false;

    // Pending invite (host side) — stored while the popup is shown
    std::string m_pendingGuestName;

    // Settings helpers
    std::string username()  const;
    bool        accepting() const;
};
