#pragma once
#include "NetworkTypes.hpp"
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

// -----------------------------------------------------------------------
//  Discovery — UDP broadcast peer discovery
//
//  * Sends an Announce packet every ANNOUNCE_INTERVAL_MS on the LAN
//    broadcast address (255.255.255.255:DISCOVERY_PORT).
//  * Listens for incoming Announce and Query packets on the same port.
//  * Calls onPeerFound when a new (or updated) peer is discovered.
// -----------------------------------------------------------------------
class Discovery {
public:
    using PeerCallback = std::function<void(const PeerInfo&)>;
    using PeerLostCallback = std::function<void(const std::string& ip)>;

    static constexpr int ANNOUNCE_INTERVAL_MS = 2000;
    static constexpr int PEER_TIMEOUT_MS      = 8000;

    Discovery() = default;
    ~Discovery() { stop(); }

    // Non-copyable
    Discovery(const Discovery&) = delete;
    Discovery& operator=(const Discovery&) = delete;

    // Start broadcasting and listening.
    bool start(const std::string& username, bool accepting);

    // Stop all threads and close sockets.
    void stop();

    // Update accepting flag (user toggled the setting mid-session).
    void setAccepting(bool accepting);

    // Get a snapshot of currently visible peers.
    std::vector<PeerInfo> peers() const;

    // Callbacks (called from background thread — dispatch to main thread as needed).
    PeerCallback     onPeerFound;
    PeerLostCallback onPeerLost;

private:
    void broadcastLoop();
    void receiveLoop();
    void evictStaleLoop();

    std::string buildAnnounceJson() const;
    void        handlePacket(const char* buf, int len, const std::string& fromIp);
    void        upsertPeer(const PeerInfo& p);

    socket_t    m_sendSock  = INVALID_SOCKET;
    socket_t    m_recvSock  = INVALID_SOCKET;

    std::string m_username;
    std::atomic<bool> m_accepting{true};
    std::atomic<bool> m_running{false};

    std::thread m_broadcastThread;
    std::thread m_receiveThread;
    std::thread m_evictThread;

    mutable std::mutex          m_peersMu;
    std::vector<PeerInfo>       m_peers;
    // last-seen timestamps (parallel to m_peers)
    std::vector<int64_t>        m_lastSeen;
};
