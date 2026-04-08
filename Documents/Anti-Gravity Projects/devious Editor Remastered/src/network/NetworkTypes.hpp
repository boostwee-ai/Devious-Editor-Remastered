#pragma once
#include <cstdint>
#include <string>
#include <vector>

// -----------------------------------------------------------------------
//  Cross-platform socket setup
// -----------------------------------------------------------------------
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    using socket_t = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <ifaddrs.h>
    using socket_t = int;
    constexpr socket_t INVALID_SOCKET = -1;
    constexpr int SOCKET_ERROR = -1;
    inline void closesocket(int fd) { ::close(fd); }
#endif

// -----------------------------------------------------------------------
//  Platform tag — used for cross-OS connection guard
// -----------------------------------------------------------------------
#ifdef _WIN32
    constexpr const char* PLATFORM_TAG = "win";
#elif defined(__APPLE__)
    constexpr const char* PLATFORM_TAG = "mac";
#elif defined(__ANDROID__)
    constexpr const char* PLATFORM_TAG = "android";
#else
    constexpr const char* PLATFORM_TAG = "unknown";
#endif

// -----------------------------------------------------------------------
//  Port configuration
// -----------------------------------------------------------------------
constexpr uint16_t DISCOVERY_PORT = 14523;   // UDP broadcast
constexpr uint16_t SESSION_PORT   = 14524;   // TCP session

// -----------------------------------------------------------------------
//  Wire message types  (1-byte discriminant, then length-prefixed JSON body)
// -----------------------------------------------------------------------
enum class MsgType : uint8_t {
    // Discovery (UDP)
    Announce   = 0x01,   // peer advertising availability
    Query      = 0x02,   // active scan request

    // Session handshake (TCP)
    ColabReq   = 0x10,   // guest → host: "join me"
    ColabAck   = 0x11,   // host → guest: accepted
    ColabNak   = 0x12,   // host → guest: rejected (includes reason)

    // In-session (TCP, bidirectional)
    Viewport   = 0x20,   // camera/cursor position
    ObjPlace   = 0x21,   // place one or more objects
    ObjDelete  = 0x22,   // delete objects by ID
    ObjEdit    = 0x23,   // edit object properties
    Disconnect = 0x2F,   // clean close
};

// -----------------------------------------------------------------------
//  Peer descriptor (populated from Announce packets)
// -----------------------------------------------------------------------
struct PeerInfo {
    std::string username;
    std::string platform;       // PLATFORM_TAG of the remote peer
    std::string ip;             // dotted-decimal
    uint16_t    tcpPort = SESSION_PORT;
    bool        accepting = true;
};

// -----------------------------------------------------------------------
//  Simple length-prefixed framing over TCP
//  Layout: [MsgType : 1 byte][bodyLen : 4 bytes LE][body : bodyLen bytes]
// -----------------------------------------------------------------------
struct Frame {
    MsgType              type;
    std::vector<uint8_t> body;   // JSON payload (UTF-8)

    std::string bodyStr() const { return std::string(body.begin(), body.end()); }
};
