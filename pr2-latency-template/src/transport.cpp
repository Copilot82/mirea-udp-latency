#include "transport/udp.hpp"
#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace transport {
namespace {
#ifdef _WIN32
using Native = SOCKET;
using AddressLength = int;
constexpr auto Invalid = INVALID_SOCKET;
int LastError() { return WSAGetLastError(); }
void Close(Native s) { closesocket(s); }
void Initialize() {
    struct Runtime {
        Runtime() { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("WSAStartup failed"); }
        ~Runtime() { WSACleanup(); }
    };
    static Runtime runtime;
}
#else
using Native = int;
using AddressLength = socklen_t;
constexpr auto Invalid = -1;
int LastError() { return errno; }
void Close(Native s) { close(s); }
void Initialize() {}
#endif
[[noreturn]] void Fail(const char* operation) {
    throw std::system_error(LastError(), std::system_category(), operation);
}
sockaddr_in Address(const Endpoint& peer) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(peer.port);
    if (inet_pton(AF_INET, peer.ip.c_str(), &address.sin_addr) != 1)
        throw std::invalid_argument("expected numeric IPv4 address");
    return address;
}
Endpoint FromAddress(const sockaddr_in& address) {
    std::array<char, INET_ADDRSTRLEN> ip{};
    if (!inet_ntop(AF_INET, &address.sin_addr, ip.data(), ip.size())) Fail("inet_ntop");
    return Endpoint(ip.data(), ntohs(address.sin_port));
}
}

std::uint64_t NowUs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
Endpoint::Endpoint(std::string text, std::uint16_t number) : ip(std::move(text)), port(number) {
    Initialize();
    const auto address = Address(*this);
    std::array<char, INET_ADDRSTRLEN> canonical{};
    if (!inet_ntop(AF_INET, &address.sin_addr, canonical.data(), canonical.size())) Fail("inet_ntop");
    ip = canonical.data();
}
std::string Endpoint::ToString() const { return ip + ':' + std::to_string(port); }
UdpSocket::UdpSocket() {
    Initialize();
    const auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == Invalid) Fail("socket");
#ifndef _WIN32
    if (s >= FD_SETSIZE) { Close(s); throw std::runtime_error("socket exceeds select capacity"); }
#endif
    handle_ = static_cast<std::uintptr_t>(s);
}
UdpSocket::~UdpSocket() { Close(static_cast<Native>(handle_)); }
void UdpSocket::Bind(const Endpoint& local) {
    const auto address = Address(local);
    if (bind(static_cast<Native>(handle_), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        Fail("bind");
}
Endpoint UdpSocket::LocalEndpoint() const {
    sockaddr_in address{};
    AddressLength size = sizeof(address);
    if (getsockname(static_cast<Native>(handle_), reinterpret_cast<sockaddr*>(&address), &size)) Fail("getsockname");
    return FromAddress(address);
}
void UdpSocket::SendTo(const Endpoint& peer, const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > 65507) throw std::length_error("UDP datagram too large");
    const auto address = Address(peer);
    const auto sent = sendto(static_cast<Native>(handle_), reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()), 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) Fail("sendto");
}
std::optional<Datagram> UdpSocket::Receive(std::uint64_t waitUs) {
    const auto s = static_cast<Native>(handle_);
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(s, &ready);
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(waitUs / 1'000'000);
    timeout.tv_usec = static_cast<long>(waitUs % 1'000'000);
#ifdef _WIN32
    const int count = select(0, &ready, nullptr, nullptr, &timeout);
    if (count < 0 && LastError() == WSAEINTR) return std::nullopt;
#else
    const int count = select(s + 1, &ready, nullptr, nullptr, &timeout);
    if (count < 0 && errno == EINTR) return std::nullopt;
#endif
    if (count < 0) Fail("select");
    if (!count) return std::nullopt;
    std::array<std::uint8_t, 65536> buffer{}; // Full UDP length, no silent truncation.
    sockaddr_in address{};
    AddressLength size = sizeof(address);
    const auto length = recvfrom(s, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()),
        0, reinterpret_cast<sockaddr*>(&address), &size);
    const auto received = NowUs();
    if (length < 0) {
#ifdef _WIN32
        if (LastError() == WSAECONNRESET || LastError() == WSAEINTR) return std::nullopt;
#else
        if (errno == EINTR) return std::nullopt;
#endif
        Fail("recvfrom");
    }
    return Datagram{FromAddress(address), {buffer.begin(), buffer.begin() + length}, received};
}
}
