#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hvr {

// Port the Hytale VR server plugin listens on, bound to 127.0.0.1 only.
constexpr uint16_t kPluginBridgePort = 47823;
constexpr int kPluginBridgeProtocol = 1;

// One hand's pointing ray: origin relative to the player's eye, unit direction,
// both in Hytale world axes.
struct HandRay {
    float origin[3]{};
    float direction[3]{};
};

// look is Hytale's own camera direction in the same axes as the ray.
std::string format_swing_message(int hand, const HandRay& ray, float speed, const float look[3]);
// role names this client to the plugin.
std::string format_hello_message(const std::string& role = "hytale-vr");

// Splits complete '\n'-terminated lines off the front of buffer, leaving any
// partial line in place.
std::vector<std::string> take_lines(std::string& buffer);

// Non-blocking TCP link from the proxy to the Hytale VR server plugin. poll() reconnects in
// the background, so callers just call it every tick and check connected().
class PluginBridge {
public:
    explicit PluginBridge(std::string role = "hytale-vr", size_t max_inbox = 64 * 1024);
    ~PluginBridge();
    PluginBridge(const PluginBridge&) = delete;
    PluginBridge& operator=(const PluginBridge&) = delete;

    // Advances connection state and returns any lines the plugin sent.
    std::vector<std::string> poll(uint64_t now_ms);
    bool connected() const { return state_ == State::Ready; }
    bool send_line(const std::string& line);
    void close();
    // Overrides the port; used by tests.
    void set_port(uint16_t port) { port_ = port; }

private:
    enum class State { Idle, Connecting, Ready };
    bool start_connect();

    uintptr_t socket_;
    State state_ = State::Idle;
    uint64_t next_attempt_ms_ = 0;
    uint64_t connect_started_ms_ = 0;
    uint16_t port_ = kPluginBridgePort;
    bool winsock_ready_ = false;
    std::string role_;
    size_t max_inbox_;
    std::string inbox_;
};

} // namespace hvr
