#pragma once
#include "NetworkTypes.hpp"
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

class Discovery {
public:
    using PeerCallback     = std::function<void(const PeerInfo&)>;
    using PeerLostCallback = std::function<void(const std::string& ip)>;

    static constexpr int ANNOUNCE_INTERVAL_MS = 2000;
    static constexpr int PEER_TIMEOUT_MS      = 8000;

    Discovery() = default;
    ~Discovery() { stop(); }

    Discovery(const Discovery&) = delete;
    Discovery& operator=(const Discovery&) = delete;

    bool start(const std::string& username, bool accepting);
    void stop();
    void setAccepting(bool accepting);

    std::vector<PeerInfo> peers() const;

    PeerCallback     onPeerFound;
    PeerLostCallback onPeerLost;

private:
    void broadcastLoop();
    void receiveLoop();
    void evictStaleLoop();
    void handlePacket(const char* buf, int len, const std::string& fromIp);
    void upsertPeer(const PeerInfo& p);

    // Opaque handles — real socket types only in Discovery.cpp
    socket_handle_t m_sendSock = INVALID_SOCK;
    socket_handle_t m_recvSock = INVALID_SOCK;

    std::string       m_username;
    std::atomic<bool> m_accepting{true};
    std::atomic<bool> m_running{false};

    std::thread m_broadcastThread;
    std::thread m_receiveThread;
    std::thread m_evictThread;

    mutable std::mutex    m_peersMu;
    std::vector<PeerInfo> m_peers;
    std::vector<int64_t>  m_lastSeen;
};
