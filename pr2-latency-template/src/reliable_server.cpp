#include "app/emulation.hpp"
#include "app/options.hpp"
#include "protocol/packet.hpp"
#include "reliability/reliable_channel.hpp"
#include "transport/udp.hpp"
#include <algorithm>
#include <csignal>
#include <iostream>
#include <map>
#include <queue>

namespace {
volatile std::sig_atomic_t running = 1;
void Stop(int) { running = 0; }
struct Reply {
    std::uint64_t due, order;
    transport::Endpoint peer;
    std::vector<std::uint8_t> bytes;
};
struct Later {
    bool operator()(const Reply& a, const Reply& b) const {
        return a.due == b.due ? a.order > b.order : a.due > b.due;
    }
};
struct Player { ReceiveWindow window; std::size_t shots = 0; };
int Run(int argc, char** argv) {
    if (argc > 5) throw std::invalid_argument("usage: reliable_server [IP] [PORT] [PROFILE] [SEED]");
    const transport::Endpoint local(argc > 1 ? argv[1] : "127.0.0.1",
        static_cast<std::uint16_t>(argc > 2 ? app::Number(argv[2], 0, 65535) : 27015));
    const std::string profile = argc > 3 ? argv[3] : "baseline";
    app::Emulator emulator(profile, static_cast<std::uint32_t>(
        argc > 4 ? app::Number(argv[4], 0, 0xffffffffULL) : 20261027));
    transport::UdpSocket socket;
    socket.Bind(local);
    std::signal(SIGINT, Stop);
    std::signal(SIGTERM, Stop);
    std::cout << std::unitbuf << "READY " << socket.LocalEndpoint().ToString()
              << " profile=" << profile << "\n";
    std::priority_queue<Reply, std::vector<Reply>, Later> replies;
    std::map<std::string, Player> players;
    std::uint64_t order = 0;
    std::uint16_t replySequence = 0;
    auto transmit = [&](const transport::Endpoint& peer, std::vector<std::uint8_t> bytes) {
        if (bytes[0] == static_cast<std::uint8_t>(PacketType::Pong)) {
            bytes.resize(bytes.size() - 8);
            WriteU64(bytes, transport::NowUs());
        }
        socket.SendTo(peer, bytes);
    };
    auto send = [&](const transport::Endpoint& peer, std::vector<std::uint8_t> bytes,
                    app::Impairment effect) {
        if (effect.drop) return;
        if (!effect.delayUs) transmit(peer, std::move(bytes));
        else if (replies.size() < 4096)
            replies.push({transport::NowUs() + effect.delayUs, order++, peer, std::move(bytes)});
        else std::cout << "queue_full\n";
    };
    while (running) {
        auto now = transport::NowUs();
        while (!replies.empty() && replies.top().due <= now) {
            transmit(replies.top().peer, replies.top().bytes);
            replies.pop();
            now = transport::NowUs();
        }
        auto waitUs = std::uint64_t{5'000};
        if (!replies.empty()) waitUs = std::min(waitUs, replies.top().due > now ? replies.top().due-now : 0);
        const auto data = socket.Receive(waitUs);
        if (!data) continue;
        const auto h = ParseHeader(data->bytes.data(), data->bytes.size());
        if (!h) {
            std::cout << "invalid_packet reason=" << PacketError(data->bytes.data(), data->bytes.size()) << '\n';
            continue;
        }
        if (h->type == PacketType::Ping) {
            PacketHeader parsed{};
            const auto ping = ParsePing(data->bytes.data(), data->bytes.size(), parsed);
            const auto effect = emulator.Next();
            send(data->peer, SerializePong(h->sequenceNumber,
                {ping->clientSendTimeUs, data->receivedAtUs, transport::NowUs()}), effect);
            continue;
        }
        if (h->type == PacketType::Movement) {
            std::cout << "movement sequence=" << h->sequenceNumber << '\n';
            continue;
        }
        if (h->type != PacketType::Shoot) continue;
        // Отдельные независимые потери для SHOOT на входе и ACK на выходе.
        const auto incoming = emulator.Next();
        if (incoming.drop) {
            std::cout << "drop_shoot sequence=" << h->sequenceNumber << '\n';
            continue;
        }
        const auto key = data->peer.ToString();
        if (!players.count(key) && players.size() >= 128) {
            std::cout << "session_limit\n";
            continue;
        }
        auto& player = players[key];
        const auto result = player.window.Observe(h->sequenceNumber);
        if (result == ReceiveResult::Stale) {
            std::cout << "stale_shoot sequence=" << h->sequenceNumber << '\n';
            continue;
        }
        const auto outgoing = emulator.Next();
        send(data->peer, SerializeAck(replySequence++, h->sequenceNumber), outgoing);
        std::cout << "ack sequence=" << h->sequenceNumber << " drop=" << outgoing.drop
                  << " delay_us=" << outgoing.delayUs << '\n';
        if (result == ReceiveResult::New) {
            ++player.shots;
            std::cout << "shoot_applied peer=" << key << " sequence=" << h->sequenceNumber
                      << " shots=" << player.shots << '\n';
        } else {
            std::cout << "duplicate_shoot sequence=" << h->sequenceNumber << '\n';
        }
    }
    return 0;
}
}
int main(int argc, char** argv) {
    try { return Run(argc, argv); }
    catch (const std::exception& e) { std::cerr << "reliable_server: " << e.what() << '\n'; return 1; }
}
