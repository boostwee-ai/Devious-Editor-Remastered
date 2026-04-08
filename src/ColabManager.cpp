#include "ColabManager.hpp"
#include "ui/CollabPanel.hpp"

#include <Geode/Geode.hpp>
#include <sstream>

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  Minimal JSON helpers
// -----------------------------------------------------------------------
static std::string jStr(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

static float jFloat(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0.f;
    pos += needle.size();
    try { return std::stof(json.substr(pos)); } catch (...) { return 0.f; }
}

// -----------------------------------------------------------------------
//  Singleton
// -----------------------------------------------------------------------
ColabManager* ColabManager::get() {
    static ColabManager inst;
    return &inst;
}

std::string ColabManager::username() const {
    return Mod::get()->getSettingValue<std::string>("username");
}

bool ColabManager::accepting() const {
    return Mod::get()->getSettingValue<bool>("accepting-requests");
}

// -----------------------------------------------------------------------
//  Lifecycle
// -----------------------------------------------------------------------
void ColabManager::onEnterEditor() {
    m_inEditor = true;
    startDiscovery();
    m_isHost  = true;
    m_session = std::make_unique<Session>();
    m_session->onFrame     = [this](const Frame& f) { onFrame(f); };
    m_session->onError     = [this](const std::string& e) { onSessionError(e); };
    m_session->onConnected = [this]() { onSessionConnected(); };
    if (!m_session->listen(username())) {
        log::warn("[Devious] Could not start session listener");
        m_session.reset();
    }
}

void ColabManager::onExitEditor() {
    m_inEditor = false;
    if (m_session) { m_session->close(); m_session.reset(); }
    stopDiscovery();
    m_presenceLayer = nullptr;
}

// -----------------------------------------------------------------------
//  Discovery
// -----------------------------------------------------------------------
void ColabManager::startDiscovery() {
    m_discovery.onPeerFound = [](const PeerInfo&) {};
    m_discovery.onPeerLost  = [this](const std::string& ip) {
        mainThread([this, ip]() {
            if (m_presenceLayer) m_presenceLayer->removePeer(ip);
        });
    };
    if (!m_discovery.start(username(), accepting()))
        log::warn("[Devious] Discovery failed to start");
}

void ColabManager::stopDiscovery() { m_discovery.stop(); }

std::vector<PeerInfo> ColabManager::peers() const { return m_discovery.peers(); }

int ColabManager::peerIndex(const std::string& ip) const {
    auto ps = m_discovery.peers();
    for (int i = 0; i < static_cast<int>(ps.size()); ++i)
        if (ps[i].ip == ip) return i;
    return -1;
}

// -----------------------------------------------------------------------
//  Invite flow
// -----------------------------------------------------------------------
void ColabManager::invitePeer(const PeerInfo& peer) {
    if (peer.platform != std::string(PLATFORM_TAG)) {
        FLAlertLayer::create(
            "Cannot Connect",
            "Cross-platform collaboration is not supported.\nYou are on <cy>" +
            std::string(PLATFORM_TAG) + "</c>, the host is on <cr>" + peer.platform + "</c>.\nBoth players must use the same OS.",
            "OK"
        )->show();
        return;
    }

    if (m_session) m_session->close();
    m_session = std::make_unique<Session>();
    m_isHost  = false;
    m_session->onFrame     = [this](const Frame& f) { onFrame(f); };
    m_session->onError     = [this](const std::string& e) { onSessionError(e); };
    m_session->onConnected = [this]() { onSessionConnected(); };

    if (!m_session->connect(peer, username())) {
        FLAlertLayer::create("Connect Failed", "Could not connect to " + peer.username + ".", "OK")->show();
        m_session = std::make_unique<Session>();
        m_isHost  = true;
        m_session->onFrame     = [this](const Frame& f) { onFrame(f); };
        m_session->onError     = [this](const std::string& e) { onSessionError(e); };
        m_session->onConnected = [this]() { onSessionConnected(); };
        m_session->listen(username());
    }
}

void ColabManager::acceptInvite() {
    Notification::create("Collaboration started!", NotificationIcon::Success)->show();
}

void ColabManager::declineInvite() {
    if (m_session) m_session->close();
    m_session = std::make_unique<Session>();
    m_isHost  = true;
    m_session->onFrame     = [this](const Frame& f) { onFrame(f); };
    m_session->onError     = [this](const std::string& e) { onSessionError(e); };
    m_session->onConnected = [this]() { onSessionConnected(); };
    m_session->listen(username());
}

void ColabManager::onIncomingInvite(const std::string& guestName) {
    m_pendingGuestName = guestName;
    mainThread([this, guestName]() {
        InviteRequestPopup::create(guestName)->show();
    });
}

// -----------------------------------------------------------------------
//  Session callbacks
// -----------------------------------------------------------------------
void ColabManager::onSessionConnected() {
    auto peer = m_session->remotePeer();
    mainThread([this, peer]() {
        if (m_presenceLayer) m_presenceLayer->addPeer(peer.ip, peer.username);
        Notification::create(peer.username + " joined your session!", NotificationIcon::Success)->show();
    });
}

void ColabManager::onSessionError(const std::string& msg) {
    auto peer = m_session ? m_session->remotePeer() : PeerInfo{};
    mainThread([this, msg, peer]() {
        if (m_presenceLayer && !peer.ip.empty())
            m_presenceLayer->removePeer(peer.ip);

        if (msg.find("cross-platform") != std::string::npos ||
            msg.find("Cross-platform") != std::string::npos) {
            FLAlertLayer::create("Connection Failed", msg, "OK")->show();
        } else if (!msg.empty() && msg != "Peer disconnected") {
            Notification::create(msg, NotificationIcon::Warning)->show();
        }

        if (m_inEditor) {
            m_session = std::make_unique<Session>();
            m_isHost  = true;
            m_session->onFrame     = [this](const Frame& f) { onFrame(f); };
            m_session->onError     = [this](const std::string& e) { onSessionError(e); };
            m_session->onConnected = [this]() { onSessionConnected(); };
            m_session->listen(username());
        }
    });
}

// -----------------------------------------------------------------------
//  Outgoing sync
// -----------------------------------------------------------------------
void ColabManager::sendViewport(float worldX, float worldY) {
    if (!isInSession()) return;
    std::ostringstream o;
    o << "{\"x\":" << worldX << ",\"y\":" << worldY << "}";
    m_session->send(MsgType::Viewport, o.str());
}

void ColabManager::sendObjPlace(const std::string& objStr) {
    if (!isInSession()) return;
    std::ostringstream o;
    o << "{\"obj\":\"";
    for (char c : objStr) { if (c == '"' || c == '\\') o << '\\'; o << c; }
    o << "\"}";
    m_session->send(MsgType::ObjPlace, o.str());
}

void ColabManager::sendObjDelete(const std::vector<int>& ids) {
    if (!isInSession() || ids.empty()) return;
    std::ostringstream o;
    o << "{\"ids\":[";
    for (size_t i = 0; i < ids.size(); ++i) { if (i) o << ','; o << ids[i]; }
    o << "]}";
    m_session->send(MsgType::ObjDelete, o.str());
}

void ColabManager::sendObjEdit(int id, const std::string& propJson) {
    if (!isInSession()) return;
    std::ostringstream o;
    o << "{\"id\":" << id << ",\"props\":" << propJson << "}";
    m_session->send(MsgType::ObjEdit, o.str());
}

// -----------------------------------------------------------------------
//  Incoming frame dispatch
// -----------------------------------------------------------------------
void ColabManager::onFrame(const Frame& f) {
    std::string body = f.bodyStr();
    switch (f.type) {
        case MsgType::Viewport:  mainThread([this, body]{ handleViewport(body);  }); break;
        case MsgType::ObjPlace:  mainThread([this, body]{ handleObjPlace(body);  }); break;
        case MsgType::ObjDelete: mainThread([this, body]{ handleObjDelete(body); }); break;
        case MsgType::ObjEdit:   mainThread([this, body]{ handleObjEdit(body);   }); break;
        default: break;
    }
}

void ColabManager::handleViewport(const std::string& body) {
    if (!m_presenceLayer) return;
    float x = jFloat(body, "x");
    float y = jFloat(body, "y");
    auto peer = m_session ? m_session->remotePeer() : PeerInfo{};
    if (!peer.ip.empty()) m_presenceLayer->updatePeerViewport(peer.ip, x, y);
}

void ColabManager::handleObjPlace(const std::string& body) {
    auto* editor = LevelEditorLayer::get();
    if (!editor) return;
    std::string objStr = jStr(body, "obj");
    if (objStr.empty()) return;
    // addObjectFromString is the correct 2.2081 API (same as used in EditorHooks)
    editor->addObjectFromString(gd::string(objStr));
    flashRemotePeer();
}

void ColabManager::handleObjDelete(const std::string& body) {
    // Object deletion by unique ID requires iterating editor objects.
    // Flash indicator to show the peer's action; full sync is handled
    // by the host retaining all changes on disconnect.
    (void)body;
    flashRemotePeer();
}

void ColabManager::handleObjEdit(const std::string& body) {
    // Object property editing sync deferred — requires verified 2.2081 API.
    (void)body;
    flashRemotePeer();
}

// -----------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------
void ColabManager::flashRemotePeer() {
    if (!m_presenceLayer) return;
    auto peer = m_session ? m_session->remotePeer() : PeerInfo{};
    if (!peer.ip.empty()) m_presenceLayer->flashPeerEdit(peer.ip);
}

bool ColabManager::isInSession() const {
    return m_session && m_session->isConnected();
}

void ColabManager::setPresenceLayer(PresenceLayer* layer) {
    m_presenceLayer = layer;
}

template<typename F>
void ColabManager::mainThread(F&& fn) {
    Loader::get()->queueInMainThread(std::forward<F>(fn));
}
