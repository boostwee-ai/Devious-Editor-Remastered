// -----------------------------------------------------------------------
//  IMPORTANT: This file skips Geode's PCH on Windows (see CMakeLists.txt)
//  so that we can include winsock2.h BEFORE any Windows headers.
// -----------------------------------------------------------------------
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    using plat_sock_t = SOCKET;
    static constexpr plat_sock_t PLAT_INVALID = INVALID_SOCKET;
    inline void plat_close(plat_sock_t s) { ::closesocket(s); }
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <ifaddrs.h>
    using plat_sock_t = int;
    static constexpr plat_sock_t PLAT_INVALID = -1;
    inline void plat_close(plat_sock_t s) { ::close(s); }
#endif

#include "Discovery.hpp"
#include <Geode/Geode.hpp>
#include <chrono>
#include <sstream>
#include <cstring>

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  Handle conversion
// -----------------------------------------------------------------------
static plat_sock_t to_sock(socket_handle_t h) { return static_cast<plat_sock_t>(h); }
static socket_handle_t from_sock(plat_sock_t s) { return static_cast<socket_handle_t>(s); }
static bool sock_valid(socket_handle_t h) { return h != INVALID_SOCK && to_sock(h) != PLAT_INVALID; }

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// -----------------------------------------------------------------------
//  Minimal JSON helpers
// -----------------------------------------------------------------------
static std::string makeAnnounceJson(const std::string& username, bool accepting) {
    std::string safe;
    for (char c : username) {
        if (c == '"' || c == '\\') safe += '\\';
        safe += c;
    }
    std::ostringstream o;
    o << "{\"u\":\"" << safe << "\",\"p\":\"" << PLATFORM_TAG << "\""
      << ",\"a\":" << (accepting ? "true" : "false")
      << ",\"port\":" << SESSION_PORT << "}";
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

static bool jBool(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    return json.substr(pos, 4) == "true";
}

static uint16_t jU16(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return SESSION_PORT;
    pos += needle.size();
    return static_cast<uint16_t>(std::stoi(json.substr(pos)));
}

// -----------------------------------------------------------------------
//  Discovery::start
// -----------------------------------------------------------------------
bool Discovery::start(const std::string& username, bool accepting) {
    m_username  = username;
    m_accepting = accepting;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    auto sendSock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sendSock == PLAT_INVALID) return false;
    { int yes = 1; ::setsockopt(sendSock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes)); }
    m_sendSock = from_sock(sendSock);

    auto recvSock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (recvSock == PLAT_INVALID) { plat_close(sendSock); m_sendSock = INVALID_SOCK; return false; }
    { int yes = 1; ::setsockopt(recvSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes)); }
#ifdef SO_REUSEPORT
    { int yes = 1; ::setsockopt(recvSock, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&yes), sizeof(yes)); }
#endif
    {
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(DISCOVERY_PORT);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (::bind(recvSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            plat_close(sendSock); plat_close(recvSock);
            m_sendSock = m_recvSock = INVALID_SOCK;
            return false;
        }
    }
#ifdef _WIN32
    { u_long mode = 1; ::ioctlsocket(recvSock, FIONBIO, &mode); }
#else
    { int f = ::fcntl(recvSock, F_GETFL, 0); ::fcntl(recvSock, F_SETFL, f | O_NONBLOCK); }
#endif
    m_recvSock = from_sock(recvSock);

    m_running = true;
    m_broadcastThread = std::thread(&Discovery::broadcastLoop, this);
    m_receiveThread   = std::thread(&Discovery::receiveLoop,   this);
    m_evictThread     = std::thread(&Discovery::evictStaleLoop, this);
    return true;
}

void Discovery::stop() {
    m_running = false;
    if (sock_valid(m_sendSock)) { plat_close(to_sock(m_sendSock)); m_sendSock = INVALID_SOCK; }
    if (sock_valid(m_recvSock)) { plat_close(to_sock(m_recvSock)); m_recvSock = INVALID_SOCK; }
    if (m_broadcastThread.joinable()) m_broadcastThread.join();
    if (m_receiveThread.joinable())   m_receiveThread.join();
    if (m_evictThread.joinable())     m_evictThread.join();
#ifdef _WIN32
    WSACleanup();
#endif
}

void Discovery::setAccepting(bool a) { m_accepting = a; }

std::vector<PeerInfo> Discovery::peers() const {
    std::lock_guard<std::mutex> lk(m_peersMu);
    return m_peers;
}

void Discovery::broadcastLoop() {
    sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(DISCOVERY_PORT);
    dest.sin_addr.s_addr = INADDR_BROADCAST;

    while (m_running) {
        if (sock_valid(m_sendSock)) {
            std::string body = makeAnnounceJson(m_username, m_accepting.load());
            std::string pkt;
            pkt += static_cast<char>(MsgType::Announce);
            pkt += body;
            ::sendto(to_sock(m_sendSock), pkt.data(), static_cast<int>(pkt.size()), 0,
                     reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ANNOUNCE_INTERVAL_MS));
    }
}

void Discovery::receiveLoop() {
    char buf[2048];
    while (m_running) {
        if (!sock_valid(m_recvSock)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        int n = static_cast<int>(::recvfrom(to_sock(m_recvSock), buf, sizeof(buf) - 1, 0,
                                             reinterpret_cast<sockaddr*>(&from), &fromLen));
        if (n <= 0) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
        buf[n] = '\0';
        std::string ip = inet_ntoa(from.sin_addr);
        handlePacket(buf, n, ip);
    }
}

void Discovery::evictStaleLoop() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        int64_t now = nowMs();
        std::lock_guard<std::mutex> lk(m_peersMu);
        for (int i = static_cast<int>(m_peers.size()) - 1; i >= 0; --i) {
            if (now - m_lastSeen[i] > PEER_TIMEOUT_MS) {
                std::string ip = m_peers[i].ip;
                m_peers.erase(m_peers.begin() + i);
                m_lastSeen.erase(m_lastSeen.begin() + i);
                if (onPeerLost) onPeerLost(ip);
            }
        }
    }
}

void Discovery::handlePacket(const char* buf, int len, const std::string& fromIp) {
    if (len < 2) return;
    auto type = static_cast<MsgType>(static_cast<uint8_t>(buf[0]));
    if (type != MsgType::Announce && type != MsgType::Query) return;
    std::string body(buf + 1, len - 1);
    PeerInfo p;
    p.ip        = fromIp;
    p.username  = jStr(body, "u");
    p.platform  = jStr(body, "p");
    p.accepting = jBool(body, "a");
    p.tcpPort   = jU16(body, "port");
    if (p.username.empty() || p.platform.empty()) return;
    if (p.username == m_username && p.platform == std::string(PLATFORM_TAG)) return;
    upsertPeer(p);
}

void Discovery::upsertPeer(const PeerInfo& p) {
    std::lock_guard<std::mutex> lk(m_peersMu);
    int64_t now = nowMs();
    for (size_t i = 0; i < m_peers.size(); ++i) {
        if (m_peers[i].ip == p.ip) {
            m_peers[i] = p; m_lastSeen[i] = now;
            if (onPeerFound) onPeerFound(p);
            return;
        }
    }
    m_peers.push_back(p);
    m_lastSeen.push_back(now);
    if (onPeerFound) onPeerFound(p);
}
