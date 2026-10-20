#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace transport {
std::uint64_t NowUs();
struct Endpoint {
    std::string ip;
    std::uint16_t port;
    Endpoint(std::string ip, std::uint16_t port);
    bool operator==(const Endpoint& other) const { return ip == other.ip && port == other.port; }
    std::string ToString() const;
};
struct Datagram {
    Endpoint peer;
    std::vector<std::uint8_t> bytes;
    std::uint64_t receivedAtUs;
};
class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    void Bind(const Endpoint& local);
    Endpoint LocalEndpoint() const;
    void SendTo(const Endpoint& peer, const std::vector<std::uint8_t>& bytes);
    std::optional<Datagram> Receive(std::uint64_t waitUs);
private:
    std::uintptr_t handle_;
};
}
