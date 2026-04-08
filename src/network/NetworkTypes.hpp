#pragma once
#include <cstdint>
#include <string>
#include <vector>

// -----------------------------------------------------------------------
//  Platform tag
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
//  Opaque socket handle.
//
//  Platform-specific socket types (SOCKET on Win32, int on POSIX) are
//  kept exclusively in .cpp files to avoid pulling winsock2/POSIX headers
//  into headers shared with Geode code (the PCH already includes windows.h
//  which would cause redefinition conflicts with winsock2.h).
// -----------------------------------------------------------------------
using socket_handle_t = intptr_t;
constexpr socket_handle_t INVALID_SOCK = static_cast<socket_handle_t>(-1);

// -----------------------------------------------------------------------
//  Wire message types  (1-byte discriminant + length-prefixed JSON body)
// -----------------------------------------------------------------------
enum class MsgType : uint8_t {
    Announce   = 0x01,
    Query      = 0x02,
    ColabReq   = 0x10,
    ColabAck   = 0x11,
    ColabNak   = 0x12,
    Viewport   = 0x20,
    ObjPlace   = 0x21,
    ObjDelete  = 0x22,
    ObjEdit    = 0x23,
    Disconnect = 0x2F,
};

// -----------------------------------------------------------------------
//  Peer descriptor
// -----------------------------------------------------------------------
struct PeerInfo {
    std::string username;
    std::string platform;
    std::string ip;
    uint16_t    tcpPort  = SESSION_PORT;
    bool        accepting = true;
};

// -----------------------------------------------------------------------
//  TCP frame: [type:1][len:4LE][body:len]
// -----------------------------------------------------------------------
struct Frame {
    MsgType              type;
    std::vector<uint8_t> body;
    std::string bodyStr() const { return std::string(body.begin(), body.end()); }
};
