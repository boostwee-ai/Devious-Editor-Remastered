#include "Discovery.hpp"
#include <Geode/Geode.hpp>

#include <chrono>
#include <sstream>
#include <cstring>

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------
static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Minimal JSON builder — avoids pulling in a JSON lib for tiny packets.
static std::string makeJson(const std::string& username,
                             const std::string& platform,
                             bool accepting,
                             uint16_t port) {
    // Escape the username (strip quotes/backslashes to keep things simple)
    std::string safe;
    for (char c : username) {
        if (c == '"' || c == '\\') safe += '\\';
        safe += c;
    }
    std::ostringstream o;
    o << "{\"u\":\"" << safe << "\""
      << ",\"p\":\"" << platform << "\""
      << ",\"a\":" << (accepting ? "true" : "false")
      << ",\"port\":" << port
      << "}";
    return o.str();
}

// Ultra-minimal key extraction (no full JSON parser needed here).
static std::string jsonStr(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

static bool jsonBool(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    return json.substr(pos, 4) == "true";
}

static uint16_t jsonU16(const std::string& json, const std::string& key) {
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
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    // -- Send socket (broadcast) --
    m_sendSock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_sendSock == INVALID_SOCKET) {
        log::error("[Devious] Failed to create send socket");
        return false;
    }
    {
        int yes = 1;
        ::setsockopt(m_sendSock, SOL_SOCKET, SO_BROADCAST,
                     reinterpret_cast<const char*>(&yes), sizeof(yes));
    }

    // -- Receive socket --
    m_recvSock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_recvSock == INVALID_SOCKET) {
        log::error("[Devious] Failed to create recv socket");
        closesocket(m_sendSock);
        m_sendSock = INVALID_SOCKET;
        return false;
    }
    {
        int yes = 1;
        ::setsockopt(m_recvSock, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&yes), sizeof(yes));
#ifdef SO_REUSEPORT
        ::setsockopt(m_recvSock, SOL_SOCKET, SO_REUSEPORT,
                     reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif
    }
    {
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(DISCOVERY_PORT);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (::bind(m_recvSock,
                   reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            log::error("[Devious] Failed to bind recv socket on port {}", DISCOVERY_PORT);
            closesocket(m_sendSock);
            closesocket(m_recvSock);
            m_sendSock = m_recvSock = INVALID_SOCKET;
            return false;
        }
    }
    // Non-blocking receive
    {
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(m_recvSock, FIONBIO, &mode);
#else
        int flags = fcntl(m_recvSock, F_GETFL, 0);
        fcntl(m_recvSock, F_SETFL, flags | O_NONBLOCK);
#endif
    }

    m_running = true;
    m_broadcastThread = std::thread(&Discovery::broadcastLoop, this);
    m_receiveThread   = std::thread(&Discovery::receiveLoop,   this);
    m_evictThread     = std::thread(&Discovery::evictStaleLoop, this);
    return true;
}

// -----------------------------------------------------------------------
//  Discovery::stop
// -----------------------------------------------------------------------
void Discovery::stop() {
    m_running = false;
    if (m_sendSock != INVALID_SOCKET) {
        closesocket(m_sendSock);
        m_sendSock = INVALID_SOCKET;
    }
    if (m_recvSock != INVALID_SOCKET) {
        closesocket(m_recvSock);
        m_recvSock = INVALID_SOCKET;
    }
    if (m_broadcastThread.joinable()) m_broadcastThread.join();
    if (m_receiveThread.joinable())   m_receiveThread.join();
    if (m_evictThread.joinable())     m_evictThread.join();
#ifdef _WIN32
    WSACleanup();
#endif
}

void Discovery::setAccepting(bool accepting) {
    m_accepting = accepting;
}

std::vector<PeerInfo> Discovery::peers() const {
    std::lock_guard<std::mutex> lk(m_peersMu);
    return m_peers;
}

// -----------------------------------------------------------------------
//  Background loops
// -----------------------------------------------------------------------
void Discovery::broadcastLoop() {
    sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(DISCOVERY_PORT);
    dest.sin_addr.s_addr = INADDR_BROADCAST;

    while (m_running) {
        std::string body = makeJson(m_username, PLATFORM_TAG, m_accepting.load(), SESSION_PORT);
        // Prepend type byte
        std::string pkt;
        pkt += static_cast<char>(MsgType::Announce);
        pkt += body;

        ::sendto(m_sendSock, pkt.data(), static_cast<int>(pkt.size()), 0,
                 reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

        std::this_thread::sleep_for(
            std::chrono::milliseconds(ANNOUNCE_INTERVAL_MS));
    }
}

void Discovery::receiveLoop() {
    char buf[2048];
    while (m_running) {
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        int n = ::recvfrom(m_recvSock, buf, sizeof(buf) - 1, 0,
                           reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
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

// -----------------------------------------------------------------------
//  Packet handling
// -----------------------------------------------------------------------
void Discovery::handlePacket(const char* buf, int len, const std::string& fromIp) {
    if (len < 2) return;
    auto type = static_cast<MsgType>(static_cast<uint8_t>(buf[0]));
    if (type != MsgType::Announce && type != MsgType::Query) return;

    std::string body(buf + 1, len - 1);

    PeerInfo p;
    p.ip        = fromIp;
    p.username  = jsonStr(body, "u");
    p.platform  = jsonStr(body, "p");
    p.accepting = jsonBool(body, "a");
    p.tcpPort   = jsonU16(body, "port");

    if (p.username.empty() || p.platform.empty()) return;

    // Don't list ourselves
    // (same-host detection: check if the broadcast comes from our own address)
    // We compare usernames as a simple heuristic; proper loopback detection
    // would enumerate local IPs. In practice two players on same machine is
    // not a supported workflow anyway.
    if (p.username == m_username && p.platform == std::string(PLATFORM_TAG)) return;

    upsertPeer(p);
}

void Discovery::upsertPeer(const PeerInfo& p) {
    std::lock_guard<std::mutex> lk(m_peersMu);
    int64_t now = nowMs();
    for (size_t i = 0; i < m_peers.size(); ++i) {
        if (m_peers[i].ip == p.ip) {
            m_peers[i]   = p;
            m_lastSeen[i] = now;
            if (onPeerFound) onPeerFound(p);
            return;
        }
    }
    m_peers.push_back(p);
    m_lastSeen.push_back(now);
    if (onPeerFound) onPeerFound(p);
}
