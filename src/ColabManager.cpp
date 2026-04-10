#include "ColabManager.hpp"
#include "ui/CollabPanel.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <sstream>

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  Minimal JSON helpers (reused across the codebase — no dependency)
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
    return std::stof(json.substr(pos));
}

static int jInt(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();
    return std::stoi(json.substr(pos));
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
    // Start listening for incoming session requests (host role)
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
    if (m_session) {
        m_session->close();
        m_session.reset();
    }
    stopDiscovery();
    m_presenceLayer = nullptr;
}

// -----------------------------------------------------------------------
//  Discovery
// -----------------------------------------------------------------------
void ColabManager::startDiscovery() {
    m_discovery.onPeerFound = [this](const PeerInfo& p) {
        // Presence indicators are updated via the panel rebuild, not here
        (void)p;
    };
    m_discovery.onPeerLost = [this](const std::string& ip) {
        mainThread([this, ip]() {
            if (m_presenceLayer) m_presenceLayer->removePeer(ip);
        });
    };
    if (!m_discovery.start(username(), accepting())) {
        log::warn("[Devious] Discovery failed to start");
    }
}

void ColabManager::stopDiscovery() {
    m_discovery.stop();
}

// -----------------------------------------------------------------------
//  Peer list
// -----------------------------------------------------------------------
std::vector<PeerInfo> ColabManager::peers() const {
    return m_discovery.peers();
}

int ColabManager::peerIndex(const std::string& ip) const {
    auto ps = m_discovery.peers();
    for (int i = 0; i < static_cast<int>(ps.size()); ++i) {
        if (ps[i].ip == ip) return i;
    }
    return -1;
}

// -----------------------------------------------------------------------
//  Guest: invite flow
// -----------------------------------------------------------------------
void ColabManager::invitePeer(const PeerInfo& peer) {
    // Cross-platform guard (also enforced in Session::connect, but show early UX)
    if (peer.platform != std::string(PLATFORM_TAG)) {
        Notification::create(
            "Cross-platform collaboration is not supported.\n"
            "You are on " + std::string(PLATFORM_TAG) +
            " but that player is on " + peer.platform + ".",
            NotificationIcon::Error
        )->show();
        return;
    }

    // Close the existing host listen socket and open as guest
    if (m_session) m_session->close();
    m_session = std::make_unique<Session>();
    m_isHost  = false;

    m_session->onFrame     = [this](const Frame& f) { onFrame(f); };
    m_session->onError     = [this](const std::string& e) { onSessionError(e); };
    m_session->onConnected = [this]() { onSessionConnected(); };

    if (!m_session->connect(peer, username())) {
        Notification::create("Could not connect to " + peer.username + ".",
                             NotificationIcon::Error)->show();
        // Restart host listener
        m_session = std::make_unique<Session>();
        m_isHost  = true;
        m_session->listen(username());
    }
}

// -----------------------------------------------------------------------
//  Host: accept/decline (called from InviteRequestPopup)
// -----------------------------------------------------------------------
void ColabManager::acceptInvite() {
    // Session::listen already handled the handshake — nothing more to do here.
    // The session is already up; presence indicator was added in onSessionConnected.
    Notification::create("Collaboration started!", NotificationIcon::Success)->show();
}

void ColabManager::declineInvite() {
    // The session socket is already open from the accept() call in listen().
    // We close it to drop the guest.
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
        auto popup = InviteRequestPopup::create(guestName);
        static_cast<geode::Popup<>*>(popup)->show();
    });
}

// -----------------------------------------------------------------------
//  Session connected
// -----------------------------------------------------------------------
void ColabManager::onSessionConnected() {
    auto peer = m_session->remotePeer();
    mainThread([this, peer]() {
        if (m_presenceLayer) {
            m_presenceLayer->addPeer(peer.ip, peer.username);
        }
        Notification::create(peer.username + " joined your session!",
                             NotificationIcon::Success)->show();
    });
}

// -----------------------------------------------------------------------
//  Session error / disconnect
// -----------------------------------------------------------------------
void ColabManager::onSessionError(const std::string& msg) {
    auto peer = m_session ? m_session->remotePeer() : PeerInfo{};
    mainThread([this, msg, peer]() {
        if (m_presenceLayer && !peer.ip.empty()) {
            m_presenceLayer->removePeer(peer.ip);
        }
        // Cross-platform errors get a prominent notification
        if (msg.find("cross-platform") != std::string::npos ||
            msg.find("Cross-platform") != std::string::npos) {
            FLAlertLayer::create(
                "Connection Failed",
                msg,
                "OK"
            )->show();
        } else if (!msg.empty() && msg != "Peer disconnected") {
            Notification::create(msg, NotificationIcon::Warning)->show();
        }

        // If we're still in the editor, restart listening as host
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
//  Sending sync messages
// -----------------------------------------------------------------------
void ColabManager::sendViewport(float worldX, float worldY) {
    if (!isInSession()) return;
    std::ostringstream o;
    o << "{\"x\":" << worldX << ",\"y\":" << worldY << "}";
    m_session->send(MsgType::Viewport, o.str());
}

void ColabManager::sendObjPlace(const std::string& objStr) {
    if (!isInSession()) return;
    // Wrap in JSON; objStr is already the GD object string
    std::ostringstream o;
    o << "{\"obj\":\"";
    for (char c : objStr) {
        if (c == '"' || c == '\\') o << '\\';
        o << c;
    }
    o << "\"}";
    m_session->send(MsgType::ObjPlace, o.str());
}

void ColabManager::sendObjDelete(const std::vector<int>& ids) {
    if (!isInSession() || ids.empty()) return;
    std::ostringstream o;
    o << "{\"ids\":[";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) o << ',';
        o << ids[i];
    }
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
    if (!peer.ip.empty()) {
        m_presenceLayer->updatePeerViewport(peer.ip, x, y);
    }
}

void ColabManager::handleObjPlace(const std::string& body) {
    auto* editor = LevelEditorLayer::get();
    if (!editor) return;

    std::string objStr = jStr(body, "obj");
    if (objStr.empty()) return;

    // We use simple object creation since addObjectFromString bindings are unreliable
    // Pattern mirrors the sister project's ActionSerializer.cpp
    // For now, only basic placement is supported via this fallback path.
    // In a future update, we can re-enable full string parsing.
    auto obj = editor->createObject(1, CCPointZero, true);
    if (!obj) return;

    // Minimum needed to show something
    obj->customSetup(objStr.c_str(), true);
    editor->m_objects->addObject(obj);

    // Flash the presence indicator
    if (m_presenceLayer) {
        auto peer = m_session ? m_session->remotePeer() : PeerInfo{};
        if (!peer.ip.empty()) m_presenceLayer->flashPeerEdit(peer.ip);
    }
}

void ColabManager::handleObjDelete(const std::string& body) {
    auto* editor = LevelEditorLayer::get();
    if (!editor) return;

    // Parse id array: {"ids":[1,2,3]}
    auto pos = body.find("\"ids\":[");
    if (pos == std::string::npos) return;
    pos += 7;
    auto end = body.find(']', pos);
    if (end == std::string::npos) return;

    std::string arr = body.substr(pos, end - pos);
    std::istringstream ss(arr);
    std::string tok;
    CCArray* toDelete = CCArray::create();
    while (std::getline(ss, tok, ',')) {
        tok.erase(0, tok.find_first_not_of(" \t"));
        if (tok.empty()) continue;
        int id = std::stoi(tok);

        // Find object manually in m_objects array
        for (auto obj : CCArrayExt<GameObject*>(editor->m_objects)) {
            if (obj->m_uniqueID == id) {
                editor->removeObject(obj, true);
                break;
            }
        }
    }

    if (m_presenceLayer) {
        auto peer = m_session ? m_session->remotePeer() : PeerInfo{};
        if (!peer.ip.empty()) m_presenceLayer->flashPeerEdit(peer.ip);
    }
}

void ColabManager::handleObjEdit(const std::string& body) {
    auto* editor = LevelEditorLayer::get();
    if (!editor) return;

    int id = jInt(body, "id");
    GameObject* found = nullptr;
    for (auto obj : CCArrayExt<GameObject*>(editor->m_objects)) {
        if (obj->m_uniqueID == id) {
            found = obj;
            break;
        }
    }
    if (!found) return;

    // Extract props JSON and apply using GD's property string API
    auto propsPos = body.find("\"props\":");
    if (propsPos == std::string::npos) return;
    // Props are stored as the GD property key=value pairs in the "props" string
    std::string props = jStr(body, "props");
    if (!props.empty()) {
        found->customSetup(props.c_str(), false);
    }

    if (m_presenceLayer) {
        auto peer = m_session ? m_session->remotePeer() : PeerInfo{};
        if (!peer.ip.empty()) m_presenceLayer->flashPeerEdit(peer.ip);
    }
}

// -----------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------
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
