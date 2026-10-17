#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include "plugin_bridge.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

// Stands in for the Java plugin: accepts one client on an ephemeral port.
struct FakePlugin {
    SOCKET listener = INVALID_SOCKET;
    SOCKET client = INVALID_SOCKET;
    uint16_t port = 0;

    bool start() {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) return false;
        int length = sizeof(address);
        getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
        port = ntohs(address.sin_port);
        return listen(listener, 1) == 0;
    }
    bool accept_client() {
        client = accept(listener, nullptr, nullptr);
        return client != INVALID_SOCKET;
    }
    std::string read_some() {
        char buffer[512];
        const int received = recv(client, buffer, sizeof(buffer), 0);
        return received > 0 ? std::string(buffer, static_cast<size_t>(received)) : std::string();
    }
    ~FakePlugin() {
        if (client != INVALID_SOCKET) closesocket(client);
        if (listener != INVALID_SOCKET) closesocket(listener);
    }
};

} // namespace

int main() {
    bool success = true;

    std::string buffer = "HELLO a\r\nHIT block\npart";
    const auto lines = hytalevr::take_lines(buffer);
    success &= check(lines.size() == 2 && lines[0] == "HELLO a" && lines[1] == "HIT block",
                     "take_lines must split complete lines and strip CR");
    success &= check(buffer == "part", "take_lines must keep the partial line");

    hytalevr::HandRay ray{};
    ray.origin[0] = 0.25f;
    ray.direction[2] = -1.0f;
    const float look[3]{0.0f, 0.0f, -1.0f};
    const std::string swing = hytalevr::format_swing_message(1, ray, 3.5f, look);
    success &= check(swing.rfind("SWING 1 0.2500 ", 0) == 0 && swing.back() == '\n',
                     "swing message must start with the hand and end with a newline");

    hytalevr::PluginBridge bridge;  // starts Winsock for the fake plugin too
    FakePlugin plugin;
    success &= check(plugin.start(), "fake plugin must listen");
    bridge.set_port(plugin.port);

    uint64_t now = 0;
    bridge.poll(now);
    success &= check(plugin.accept_client(), "bridge must connect");
    for (int i = 0; i < 200 && !bridge.connected(); ++i) {
        Sleep(5);
        now += 5;
        bridge.poll(now);
    }
    success &= check(bridge.connected(), "bridge must reach the ready state");
    success &= check(plugin.read_some().rfind("HELLO hytale-vr 1", 0) == 0,
                     "bridge must greet the plugin");

    const std::string reply = "HELLO hytale-vr-plugin 1\nHIT entity\n";
    send(plugin.client, reply.data(), static_cast<int>(reply.size()), 0);
    std::vector<std::string> received;
    for (int i = 0; i < 200 && received.size() < 2; ++i) {
        Sleep(5);
        now += 5;
        for (auto& line : bridge.poll(now)) received.push_back(line);
    }
    success &= check(received.size() == 2 && received[1] == "HIT entity",
                     "bridge must deliver plugin lines");

    success &= check(bridge.send_line(swing), "bridge must send while connected");
    success &= check(plugin.read_some() == swing, "plugin must receive the swing line");

    closesocket(plugin.client);
    plugin.client = INVALID_SOCKET;
    for (int i = 0; i < 200 && bridge.connected(); ++i) {
        Sleep(5);
        now += 5;
        bridge.poll(now);
    }
    success &= check(!bridge.connected(), "bridge must notice the plugin going away");

    if (success) std::cout << "plugin bridge tests passed\n";
    return success ? 0 : 1;
}
