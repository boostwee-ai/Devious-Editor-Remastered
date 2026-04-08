#pragma once
#include "NetworkTypes.hpp"
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>

class Session {
public:
    using FrameCallback = std::function<void(const Frame&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    Session() = default;
    ~Session() { close(); }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool listen(const std::string& username);
    bool connect(const PeerInfo& host, const std::string& username);
    bool send(MsgType type, const std::string& jsonBody);
    void close();

    bool     isConnected() const { return m_connected; }
    PeerInfo remotePeer()  const;

    FrameCallback          onFrame;
    ErrorCallback          onError;
    std::function<void()>  onConnected;

private:
    bool doHandshakeAsHost(socket_handle_t conn, const std::string& username);
    bool doHandshakeAsGuest(const std::string& username);
    void receiveLoop();
    bool recvFrame(Frame& out);
    bool sendRaw(socket_handle_t s, MsgType type, const std::string& body);

    // Opaque handles — real socket types only in Session.cpp
    socket_handle_t m_listenSock = INVALID_SOCK;
    socket_handle_t m_sock       = INVALID_SOCK;

    std::atomic<bool> m_connected{false};
    std::thread       m_receiveThread;
    std::thread       m_acceptThread;

    mutable std::mutex m_sendMu;
    mutable std::mutex m_peerMu;
    PeerInfo           m_remotePeer;
};
