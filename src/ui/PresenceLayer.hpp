#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <unordered_map>

using namespace geode::prelude;

// -----------------------------------------------------------------------
//  PresenceIndicator — a single remote user's cursor marker.
//
//  Drawn as:
//    * A white vertical line spanning the full editor height.
//    * The user's GD player icon (SimplePlayer) centered on the line,
//      at the vertical midpoint of the visible viewport.
//    * A small name tag below the icon.
// -----------------------------------------------------------------------
class PresenceIndicator : public CCNode {
public:
    static PresenceIndicator* create(const std::string& username, int iconID, int color1, int color2);

    // Update the screen-space X position of this indicator.
    void setViewportX(float worldX, CCNode* editorLayer);

    // Pulse animation when the remote user makes an edit.
    void flashEdit();

private:
    bool init(const std::string& username, int iconID, int color1, int color2);

    CCLayerColor*   m_line    = nullptr;
    CCNode*         m_icon    = nullptr;
    CCLabelBMFont*  m_nameTag = nullptr;
};

// -----------------------------------------------------------------------
//  PresenceLayer — CCLayer added on top of LevelEditorLayer.
//  Owns all PresenceIndicators for the current session.
// -----------------------------------------------------------------------
class PresenceLayer : public CCLayer {
public:
    static PresenceLayer* create();

    // Show / remove a peer's indicator.
    void addPeer(const std::string& ip,
                 const std::string& username,
                 int iconID = 1,
                 int color1 = 0,
                 int color2 = 3);
    void removePeer(const std::string& ip);

    // Called when a viewport update arrives for a peer.
    void updatePeerViewport(const std::string& ip, float worldX, float worldY);

    // Flash the indicator when the peer makes an edit.
    void flashPeerEdit(const std::string& ip);

    // Forward camera movement so indicators stay in world-space.
    void onEditorUpdate(float dt);

private:
    bool init() override;

    std::unordered_map<std::string, PresenceIndicator*> m_indicators;
    std::unordered_map<std::string, CCPoint>            m_worldPos;
};
