#include "app/emulation.hpp"
#include "app/options.hpp"
#include "protocol/packet.hpp"
#include "transport/udp.hpp"
#include <algorithm>
#include <csignal>
#include <iostream>
#include <queue>
#include <stdexcept>

namespace {
volatile std::sig_atomic_t running = 1;
void Stop(int) { running = 0; }
struct Reply {
    std::uint64_t dueUs, order, clientUs, receivedUs;
    std::uint16_t seq;
    transport::Endpoint peer;
};
struct Later {
    bool operator()(const Reply& a, const Reply& b) const {
        return a.dueUs == b.dueUs ? a.order > b.order : a.dueUs > b.dueUs;
    }
};
int Run(int argc, char** argv) {
    if (argc > 5) throw std::invalid_argument("usage: server [BIND_IP] [PORT] [PROFILE] [SEED]");
    const transport::Endpoint local(argc > 1 ? argv[1] : "127.0.0.1",
        static_cast<std::uint16_t>(argc > 2 ? app::Number(argv[2], 0, 65535) : 27015));
    const std::string profile = argc > 3 ? argv[3] : "baseline";
    const auto seed = static_cast<std::uint32_t>(argc > 4 ? app::Number(argv[4], 0, 0xffffffffULL) : 20261020);
    app::Emulator emulator(profile, seed);
    transport::UdpSocket socket;
    socket.Bind(local);
    std::signal(SIGINT, Stop);
    std::signal(SIGTERM, Stop);
    std::cout << std::unitbuf << "READY " << socket.LocalEndpoint().ToString()
              << " profile=" << profile << " seed=" << seed << " emulator=1\n";
    std::priority_queue<Reply, std::vector<Reply>, Later> pending;
    std::uint64_t order = 0;
    while (running) {
        auto now = transport::NowUs();
        while (!pending.empty() && pending.top().dueUs <= now) {
            const auto reply = pending.top();
            pending.pop();
            const auto sendUs = transport::NowUs();
            socket.SendTo(reply.peer, SerializePong(reply.seq, {reply.clientUs, reply.receivedUs, sendUs}));
            std::cout << "pong sequence=" << reply.seq << " received_us=" << reply.receivedUs
                      << " sent_us=" << sendUs << '\n';
            now = transport::NowUs();
        }
        auto waitUs = std::uint64_t{20'000};
        if (!pending.empty()) waitUs = std::min(waitUs, pending.top().dueUs > now ? pending.top().dueUs - now : 0);
        const auto data = socket.Receive(waitUs);
        if (!data) continue;
        const auto header = ParseHeader(data->bytes.data(), data->bytes.size());
        if (!header) {
            std::cout << "invalid_packet peer=" << data->peer.ToString() << " bytes=" << data->bytes.size()
                      << " reason=" << PacketError(data->bytes.data(), data->bytes.size()) << '\n';
            continue;
        }
        if (header->type == PacketType::Movement || header->type == PacketType::Shoot) {
            std::cout << "command type=" << int(header->type) << " sequence=" << header->sequenceNumber << '\n';
            continue;
        }
        if (header->type != PacketType::Ping) {
            std::cout << "invalid_packet reason=expected_ping\n";
            continue;
        }
        PacketHeader parsed{};
        const auto ping = ParsePing(data->bytes.data(), data->bytes.size(), parsed);
        const auto effect = emulator.Next();
        std::cout << "ping sequence=" << parsed.sequenceNumber << " peer=" << data->peer.ToString()
                  << " drop=" << effect.drop << " delay_us=" << effect.delayUs << '\n';
        if (effect.drop) continue;
        if (pending.size() >= 4096) {
            std::cout << "queue_full sequence=" << parsed.sequenceNumber << '\n';
            continue;
        }
        pending.push({data->receivedAtUs + effect.delayUs, order++, ping->clientSendTimeUs,
                      data->receivedAtUs, parsed.sequenceNumber, data->peer});
    }
    std::cout << "STOP queued=" << pending.size() << '\n';
    return 0;
}
}
int main(int argc, char** argv) {
    try { return Run(argc, argv); }
    catch (const std::exception& e) { std::cerr << "server: " << e.what() << '\n'; return 1; }
}
