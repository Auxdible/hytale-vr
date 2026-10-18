#include <winsock2.h>
#include <ws2tcpip.h>

#include "plugin_bridge.h"

#include <cstdio>
#include <utility>

namespace hvr {
namespace {

constexpr uint64_t kReconnectDelayMs = 2000;
constexpr uint64_t kConnectTimeoutMs = 1500;

SOCKET as_socket(uintptr_t value) { return static_cast<SOCKET>(value); }

} // namespace

std::string format_swing_message(int hand, const HandRay& ray, float speed, const float look[3]) {
    char text[320]{};
    std::snprintf(text, sizeof(text),
                  "SWING %d %.4f %.4f %.4f %.5f %.5f %.5f %.3f %.5f %.5f %.5f\n",
                  hand == 0 ? 0 : 1,
                  ray.origin[0], ray.origin[1], ray.origin[2],
                  ray.direction[0], ray.direction[1], ray.direction[2], speed,
                  look[0], look[1], look[2]);
    return text;
}

std::string format_hello_message(const std::string& role) {
    return "HELLO " + role + " " + std::to_string(kPluginBridgeProtocol) + "\n";
}

std::vector<std::string> take_lines(std::string& buffer) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (;;) {
        const size_t end = buffer.find('\n', start);
        if (end == std::string::npos) break;
        std::string line = buffer.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(std::move(line));
        start = end + 1;
    }
    buffer.erase(0, start);
    return lines;
}

PluginBridge::PluginBridge(std::string role, size_t max_inbox)
    : socket_(static_cast<uintptr_t>(INVALID_SOCKET)), role_(std::move(role)), max_inbox_(max_inbox) {
    WSADATA data{};
    winsock_ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

PluginBridge::~PluginBridge() {
    close();
    if (winsock_ready_) WSACleanup();
}

void PluginBridge::close() {
    if (as_socket(socket_) != INVALID_SOCKET) {
        closesocket(as_socket(socket_));
        socket_ = static_cast<uintptr_t>(INVALID_SOCKET);
    }
    state_ = State::Idle;
    inbox_.clear();
}

bool PluginBridge::start_connect() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    u_long non_blocking = 1;
    ioctlsocket(s, FIONBIO, &non_blocking);
    BOOL no_delay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay),
               sizeof(no_delay));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int result = connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(s);
        return false;
    }
    socket_ = static_cast<uintptr_t>(s);
    state_ = State::Connecting;
    return true;
}

bool PluginBridge::send_line(const std::string& line) {
    if (state_ != State::Ready) return false;
    // Messages are tiny, so a short send means the link is unhealthy: drop it
    // and let poll() reconnect rather than queueing stale swings.
    const int sent = send(as_socket(socket_), line.data(), static_cast<int>(line.size()), 0);
    if (sent != static_cast<int>(line.size())) {
        close();
        return false;
    }
    return true;
}

std::vector<std::string> PluginBridge::poll(uint64_t now_ms) {
    if (!winsock_ready_) return {};
    if (state_ == State::Idle) {
        if (now_ms < next_attempt_ms_) return {};
        next_attempt_ms_ = now_ms + kReconnectDelayMs;
        connect_started_ms_ = now_ms;
        if (!start_connect()) return {};
    }
    if (state_ == State::Connecting) {
        fd_set writable;
        fd_set failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(as_socket(socket_), &writable);
        FD_SET(as_socket(socket_), &failed);
        timeval no_wait{};
        const int ready = select(0, nullptr, &writable, &failed, &no_wait);
        if (ready > 0 && FD_ISSET(as_socket(socket_), &failed)) {
            close();
            return {};
        }
        if (ready > 0 && FD_ISSET(as_socket(socket_), &writable)) {
            state_ = State::Ready;
            if (!send_line(format_hello_message(role_))) return {};
        } else if (now_ms - connect_started_ms_ > kConnectTimeoutMs) {
            close();
            return {};
        } else {
            return {};
        }
    }

    char buffer[16384];
    for (;;) {
        const int received = recv(as_socket(socket_), buffer, sizeof(buffer), 0);
        if (received > 0) {
            inbox_.append(buffer, static_cast<size_t>(received));
            if (inbox_.size() > max_inbox_) {
                close();
                return {};
            }
            continue;
        }
        if (received == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
            close();
            return {};
        }
        break;
    }
    return take_lines(inbox_);
}

} // namespace hvr
