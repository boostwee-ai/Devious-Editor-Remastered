// -----------------------------------------------------------------------
//  IMPORTANT: This file skips Geode's PCH on Windows (see CMakeLists.txt)
//  so that we can include winsock2.h BEFORE any Windows headers.
// -----------------------------------------------------------------------
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using plat_sock_t = SOCKET;
    static constexpr plat_sock_t PLAT_INVALID = INVALID_SOCKET;
    inline void     plat_close(plat_sock_t s) { ::closesocket(s); }
    inline int      plat_send(plat_sock_t s, const char* b, int l, int) { return ::send(s, b, l, 0); }
    inline int      plat_recv(plat_sock_t s, char* b, int l, int f)     { return ::recv(s, b, l, f); }
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using plat_sock_t = int;
    static constexpr plat_sock_t PLAT_INVALID = -1;
    inline void     plat_close(plat_sock_t s) { ::close(s); }
    inline int      plat_send(plat_sock_t s, const char* b, int l, int) { return ::send(s, b, l, MSG_NOSIGNAL); }
    inline int      plat_recv(plat_sock_t s, char* b, int l, int f)     { return ::recv(s, b, l, f); }
#endif

#include "Session.hpp"
#include <Geode/Geode.hpp>
#include <cstring>
#include <sstream>

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  Handle conversion
// -----------------------------------------------------------------------
static plat_sock_t to_sock(socket_handle_t h)  { return static_cast<plat_sock_t>(h); }
static socket_handle_t from_sock(plat_sock_t s) { return static_cast<socket_handle_t>(s); }
static bool sock_valid(socket_handle_t h) { return h != INVALID_SOCK && to_sock(h) != PLAT_INVALID; }

// -----------------------------------------------------------------------
//  Low-level send/recv helpers
// -----------------------------------------------------------------------
static bool recvAll(plat_sock_t s, char* buf, int len) {
    int total = 0;
    while (total < len) {
        int n = plat_recv(s, buf + total, len - total, 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

static bool sendAll(plat_sock_t s, const char* buf, int len) {
    int total = 0;
    while (total < len) {
        int n = plat_send(s, buf + total, len - total, 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

// -----------------------------------------------------------------------
//  Minimal JSON helpers
// -----------------------------------------------------------------------
static std::string handshakeJson(const std::string& username) {
    std::ostringstream o;
    o << "{\"u\":\"" << username << "\",\"p\":\"" << PLATFORM_TAG << "\"}";
    return o.str();
}

static std::string jStr(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

// -----------------------------------------------------------------------
//  Frame I/O  — layout: [type:1][len:4LE][body:len]
// -----------------------------------------------------------------------
bool Session::sendRaw(socket_handle_t h, MsgType type, const std::string& body) {
    if (!sock_valid(h)) return false;
    plat_sock_t s = to_sock(h);
    uint32_t len = static_cast<uint32_t>(body.size());
    uint8_t hdr[5];
    hdr[0] = static_cast<uint8_t>(type);
    hdr[1] = static_cast<uint8_t>(len & 0xFF);
    hdr[2] = static_cast<uint8_t>((len >> 8)  & 0xFF);
    hdr[3] = static_cast<uint8_t>((len >> 16) & 0xFF);
    hdr[4] = static_cast<uint8_t>((len >> 24) & 0xFF);
    if (!sendAll(s, reinterpret_cast<char*>(hdr), 5)) return false;
    if (len > 0 && !sendAll(s, body.data(), static_cast<int>(len))) return false;
    return true;
}

bool Session::recvFrame(Frame& out) {
    if (!sock_valid(m_sock)) return false;
    plat_sock_t s = to_sock(m_sock);
    uint8_t hdr[5];
    if (!recvAll(s, reinterpret_cast<char*>(hdr), 5)) return false;
    out.type = static_cast<MsgType>(hdr[0]);
    uint32_t len = hdr[1]
                 | (static_cast<uint32_t>(hdr[2]) << 8)
                 | (static_cast<uint32_t>(hdr[3]) << 16)
                 | (static_cast<uint32_t>(hdr[4]) << 24);
    out.body.resize(len);
    if (len > 0 && !recvAll(s, reinterpret_cast<char*>(out.body.data()), static_cast<int>(len)))
        return false;
    return true;
}

// -----------------------------------------------------------------------
//  Session::listen  (host)
// -----------------------------------------------------------------------
bool Session::listen(const std::string& username) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    auto listenSock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == PLAT_INVALID) return false;
    { int yes = 1; ::setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes)); }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(SESSION_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        plat_close(listenSock); return false;
    }
    ::listen(listenSock, 1);
    m_listenSock = from_sock(listenSock);

    m_acceptThread = std::thread([this, username]() {
        plat_sock_t ls = to_sock(m_listenSock);
        sockaddr_in clientAddr{};
        socklen_t   clientLen = sizeof(clientAddr);
        plat_sock_t conn = ::accept(ls, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

        plat_close(ls);
        m_listenSock = INVALID_SOCK;

        if (conn == PLAT_INVALID) return;
        m_sock = from_sock(conn);

        if (!doHandshakeAsHost(m_sock, username)) {
            sendRaw(m_sock, MsgType::ColabNak, R"({"reason":"cross-platform connections are not supported"})");
            plat_close(conn);
            m_sock = INVALID_SOCK;
            if (onError) onError("Cross-platform connection rejected");
            return;
        }

        m_connected = true;
        if (onConnected) onConnected();
        receiveLoop();
    });
    return true;
}

// -----------------------------------------------------------------------
//  Session::connect  (guest)
// -----------------------------------------------------------------------
bool Session::connect(const PeerInfo& host, const std::string& username) {
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
    auto s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == PLAT_INVALID) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(host.tcpPort);
    ::inet_pton(AF_INET, host.ip.c_str(), &addr.sin_addr);

    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        plat_close(s); return false;
    }
    m_sock = from_sock(s);

    if (!doHandshakeAsGuest(username)) {
        plat_close(s); m_sock = INVALID_SOCK; return false;
    }

    m_connected = true;
    if (onConnected) onConnected();
    m_receiveThread = std::thread(&Session::receiveLoop, this);
    return true;
}

// -----------------------------------------------------------------------
//  Handshakes
// -----------------------------------------------------------------------
bool Session::doHandshakeAsHost(socket_handle_t h, const std::string& username) {
    Frame req;
    socket_handle_t saved = m_sock;
    m_sock = h;
    bool ok = recvFrame(req);
    m_sock = saved;
    if (!ok || req.type != MsgType::ColabReq) return false;

    std::string body = req.bodyStr();
    std::string guestPlatform = jStr(body, "p");
    std::string guestUsername = jStr(body, "u");
    if (guestPlatform != PLATFORM_TAG) return false;

    {
        std::lock_guard<std::mutex> lk(m_peerMu);
        m_remotePeer.username = guestUsername;
        m_remotePeer.platform = guestPlatform;
    }
    sendRaw(h, MsgType::ColabAck, handshakeJson(username));
    return true;
}

bool Session::doHandshakeAsGuest(const std::string& username) {
    if (!sendRaw(m_sock, MsgType::ColabReq, handshakeJson(username))) return false;
    Frame resp;
    if (!recvFrame(resp)) return false;
    if (resp.type == MsgType::ColabNak) {
        if (onError) onError("Rejected: " + jStr(resp.bodyStr(), "reason"));
        return false;
    }
    if (resp.type != MsgType::ColabAck) return false;
    std::string body = resp.bodyStr();
    {
        std::lock_guard<std::mutex> lk(m_peerMu);
        m_remotePeer.username = jStr(body, "u");
        m_remotePeer.platform = jStr(body, "p");
    }
    return true;
}

// -----------------------------------------------------------------------
//  send / receiveLoop / close
// -----------------------------------------------------------------------
bool Session::send(MsgType type, const std::string& jsonBody) {
    if (!m_connected) return false;
    std::lock_guard<std::mutex> lk(m_sendMu);
    return sendRaw(m_sock, type, jsonBody);
}

void Session::receiveLoop() {
    Frame f;
    while (m_connected && sock_valid(m_sock)) {
        if (!recvFrame(f)) break;
        if (f.type == MsgType::Disconnect) break;
        if (onFrame) onFrame(f);
    }
    m_connected = false;
    if (onError) onError("Peer disconnected");
}

void Session::close() {
    if (m_connected) sendRaw(m_sock, MsgType::Disconnect, "{}");
    m_connected = false;
    if (sock_valid(m_listenSock)) { plat_close(to_sock(m_listenSock)); m_listenSock = INVALID_SOCK; }
    if (sock_valid(m_sock))       { plat_close(to_sock(m_sock));       m_sock       = INVALID_SOCK; }
    if (m_receiveThread.joinable()) m_receiveThread.join();
    if (m_acceptThread.joinable())  m_acceptThread.join();
}

PeerInfo Session::remotePeer() const {
    std::lock_guard<std::mutex> lk(m_peerMu);
    return m_remotePeer;
}
