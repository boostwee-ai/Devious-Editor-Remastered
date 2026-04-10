#pragma once
#include "NetworkTypes.hpp"
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <vector>
#include <string>

// -----------------------------------------------------------------------
//  Session — manages a TCP collab session.
//
//  Host role:
//    * listen() on SESSION_PORT
//    * accept() incoming connection from guest
//    * exchange handshake, verify same OS
//
//  Guest role:
//    * connect() to host IP:SESSION_PORT
//    * send ColabReq, wait for ColabAck/Nak
//
//  Both roles then share the same send/receive API.
// -----------------------------------------------------------------------
class Session {
public:
    using FrameCallback = std::function<void(const Frame&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    Session() = default;
    ~Session() { close(); }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // --- Host side ---
    // Start listening for one incoming guest connection. Calls onFrame when
    // a complete frame arrives after a successful handshake.
    bool listen(const std::string& username);

    // --- Guest side ---
    // Connect to host. Returns false immediately if OS mismatch detected.
    bool connect(const PeerInfo& host, const std::string& username);

    // Send a frame to the peer. Thread-safe.
    bool send(MsgType type, const std::string& jsonBody);

    // Disconnect cleanly.
    void close();

    bool isConnected() const { return m_connected; }

    // Peer info for the remote side (available after handshake).
    PeerInfo remotePeer() const;

    FrameCallback onFrame;   // called from receive thread
    ErrorCallback onError;   // called on disconnect / error
    std::function<void()> onConnected;  // called once handshake is done

private:
    bool doHandshakeAsHost(socket_t conn, const std::string& username);
    bool doHandshakeAsGuest(const std::string& username);

    void receiveLoop();
    bool recvFrame(Frame& out);
    bool sendRaw(socket_t s, MsgType type, const std::string& body);

    socket_t m_listenSock = INVALID_SOCKET;
    socket_t m_sock       = INVALID_SOCKET;

    std::atomic<bool> m_connected{false};
    std::thread       m_receiveThread;
    std::thread       m_acceptThread;

    mutable std::mutex m_sendMu;
    mutable std::mutex m_peerMu;
    PeerInfo           m_remotePeer;
};
