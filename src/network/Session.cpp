#include "Session.hpp"
#include <Geode/Geode.hpp>

#include <cstring>
#include <sstream>
#include <algorithm>

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  Wire helpers
// -----------------------------------------------------------------------
static bool recvAll(socket_t s, char* buf, int len) {
    int total = 0;
    while (total < len) {
        int n = ::recv(s, buf + total, len - total, 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

static bool sendAll(socket_t s, const char* buf, int len) {
    int total = 0;
    while (total < len) {
#ifdef _WIN32
        int n = ::send(s, buf + total, len - total, 0);
#else
        int n = ::send(s, buf + total, len - total, MSG_NOSIGNAL);
#endif
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

// Minimal JSON for handshake
static std::string handshakeJson(const std::string& username) {
    std::ostringstream o;
    o << "{\"u\":\"" << username << "\",\"p\":\"" << PLATFORM_TAG << "\"}";
    return o.str();
}

static std::string jsonStr(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

// -----------------------------------------------------------------------
//  Session::sendRaw — frame layout: [type:1][len:4LE][body:len]
// -----------------------------------------------------------------------
bool Session::sendRaw(socket_t s, MsgType type, const std::string& body) {
    uint32_t len = static_cast<uint32_t>(body.size());
    uint8_t  hdr[5];
    hdr[0] = static_cast<uint8_t>(type);
    hdr[1] = static_cast<uint8_t>(len & 0xFF);
    hdr[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
    hdr[3] = static_cast<uint8_t>((len >> 16) & 0xFF);
    hdr[4] = static_cast<uint8_t>((len >> 24) & 0xFF);

    if (!sendAll(s, reinterpret_cast<char*>(hdr), 5)) return false;
    if (len > 0 && !sendAll(s, body.data(), static_cast<int>(len))) return false;
    return true;
}

bool Session::recvFrame(Frame& out) {
    uint8_t hdr[5];
    if (!recvAll(m_sock, reinterpret_cast<char*>(hdr), 5)) return false;

    out.type = static_cast<MsgType>(hdr[0]);
    uint32_t len = hdr[1]
                 | (static_cast<uint32_t>(hdr[2]) << 8)
                 | (static_cast<uint32_t>(hdr[3]) << 16)
                 | (static_cast<uint32_t>(hdr[4]) << 24);

    out.body.resize(len);
    if (len > 0 && !recvAll(m_sock,
                             reinterpret_cast<char*>(out.body.data()),
                             static_cast<int>(len))) {
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------
//  Session::listen  (host side)
// -----------------------------------------------------------------------
bool Session::listen(const std::string& username) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    m_listenSock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSock == INVALID_SOCKET) return false;

    {
        int yes = 1;
        ::setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&yes), sizeof(yes));
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(SESSION_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(m_listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        log::error("[Devious] Session bind failed");
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
        return false;
    }
    ::listen(m_listenSock, 1);

    m_acceptThread = std::thread([this, username]() {
        sockaddr_in clientAddr{};
        socklen_t   clientLen = sizeof(clientAddr);
        socket_t conn = ::accept(m_listenSock,
                                  reinterpret_cast<sockaddr*>(&clientAddr),
                                  &clientLen);
        if (conn == INVALID_SOCKET) return;

        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;

        if (!doHandshakeAsHost(conn, username)) {
            sendRaw(conn, MsgType::ColabNak, R"({"reason":"cross-platform connections are not supported"})");
            closesocket(conn);
            if (onError) onError("Cross-platform connection rejected");
            return;
        }

        m_sock      = conn;
        m_connected = true;
        if (onConnected) onConnected();
        receiveLoop();
    });
    return true;
}

// -----------------------------------------------------------------------
//  Session::connect  (guest side)
// -----------------------------------------------------------------------
bool Session::connect(const PeerInfo& host, const std::string& username) {
    // --- Cross-platform guard ---
    if (host.platform != PLATFORM_TAG) {
        std::string msg =
            "Cross-platform collaboration is not supported.\n"
            "You are on " + std::string(PLATFORM_TAG) +
            ", but the host is on " + host.platform + ".\n"
            "Both players must use the same operating system.";
        log::warn("[Devious] {}", msg);
        if (onError) onError(msg);
        return false;
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    m_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_sock == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(host.tcpPort);
    ::inet_pton(AF_INET, host.ip.c_str(), &addr.sin_addr);

    if (::connect(m_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        log::error("[Devious] TCP connect to {} failed", host.ip);
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
    }

    if (!doHandshakeAsGuest(username)) {
        close();
        return false;
    }

    m_connected = true;
    if (onConnected) onConnected();

    m_receiveThread = std::thread(&Session::receiveLoop, this);
    return true;
}

// -----------------------------------------------------------------------
//  Handshakes
// -----------------------------------------------------------------------
bool Session::doHandshakeAsHost(socket_t conn, const std::string& username) {
    // Receive guest's ColabReq
    Frame req;
    m_sock = conn;  // temporarily needed by recvFrame
    if (!recvFrame(req) || req.type != MsgType::ColabReq) {
        m_sock = INVALID_SOCKET;
        return false;
    }

    std::string body = req.bodyStr();
    std::string guestPlatform = jsonStr(body, "p");
    std::string guestUsername = jsonStr(body, "u");

    if (guestPlatform != PLATFORM_TAG) {
        m_sock = INVALID_SOCKET;
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(m_peerMu);
        m_remotePeer.username = guestUsername;
        m_remotePeer.platform = guestPlatform;
    }

    sendRaw(conn, MsgType::ColabAck, handshakeJson(username));
    m_sock = INVALID_SOCKET;  // caller sets m_sock = conn after this
    return true;
}

bool Session::doHandshakeAsGuest(const std::string& username) {
    // Send ColabReq
    if (!sendRaw(m_sock, MsgType::ColabReq, handshakeJson(username))) return false;

    // Wait for ColabAck or ColabNak
    Frame resp;
    if (!recvFrame(resp)) return false;

    if (resp.type == MsgType::ColabNak) {
        std::string reason = jsonStr(resp.bodyStr(), "reason");
        if (onError) onError("Rejected: " + reason);
        return false;
    }
    if (resp.type != MsgType::ColabAck) return false;

    std::string body = resp.bodyStr();
    {
        std::lock_guard<std::mutex> lk(m_peerMu);
        m_remotePeer.username = jsonStr(body, "u");
        m_remotePeer.platform = jsonStr(body, "p");
    }
    return true;
}

// -----------------------------------------------------------------------
//  Session::send
// -----------------------------------------------------------------------
bool Session::send(MsgType type, const std::string& jsonBody) {
    if (!m_connected || m_sock == INVALID_SOCKET) return false;
    std::lock_guard<std::mutex> lk(m_sendMu);
    return sendRaw(m_sock, type, jsonBody);
}

// -----------------------------------------------------------------------
//  Session::receiveLoop
// -----------------------------------------------------------------------
void Session::receiveLoop() {
    Frame f;
    while (m_connected && m_sock != INVALID_SOCKET) {
        if (!recvFrame(f)) break;
        if (f.type == MsgType::Disconnect) break;
        if (onFrame) onFrame(f);
    }
    m_connected = false;
    if (onError) onError("Peer disconnected");
}

// -----------------------------------------------------------------------
//  Session::close
// -----------------------------------------------------------------------
void Session::close() {
    if (m_connected) {
        sendRaw(m_sock, MsgType::Disconnect, "{}");
    }
    m_connected = false;
    if (m_listenSock != INVALID_SOCKET) {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
    }
    if (m_sock != INVALID_SOCKET) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    if (m_receiveThread.joinable()) m_receiveThread.join();
    if (m_acceptThread.joinable())  m_acceptThread.join();
}

PeerInfo Session::remotePeer() const {
    std::lock_guard<std::mutex> lk(m_peerMu);
    return m_remotePeer;
}
