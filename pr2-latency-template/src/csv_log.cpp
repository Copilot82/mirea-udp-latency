#include "app/csv_log.hpp"
#include <filesystem>
#include <iomanip>
#include <locale>
#include <stdexcept>
#include <utility>

namespace app {
namespace {
constexpr const char* Header = "experiment_id;sample;sequence;sent_at_ms;rtt_ms;srtt_ms;status;observed_at_ms";
}
CsvLog::CsvLog(const std::string& path, std::string experiment, std::uint64_t startUs)
    : experiment_(std::move(experiment)), startUs_(startUs) {
    const bool nonempty = std::filesystem::exists(path) && std::filesystem::file_size(path) > 0;
    if (nonempty) {
        std::ifstream existing(path);
        std::string first;
        std::getline(existing, first);
        if (!first.empty() && first.back() == '\r') first.pop_back();
        if (first != Header) throw std::runtime_error("CSV schema mismatch");
    }
    stream_.exceptions(std::ios::failbit | std::ios::badbit);
    stream_.open(path, std::ios::app);
    stream_.imbue(std::locale::classic());
    stream_ << std::fixed << std::setprecision(6);
    if (!nonempty) stream_ << Header << '\n';
    stream_.flush();
}
void CsvLog::Write(const MeasurementEvent& e) {
    stream_ << experiment_ << ';' << e.sample << ';' << e.sequence << ';';
    if (e.sentAtUs) stream_ << double(*e.sentAtUs - startUs_) / 1000.0;
    stream_ << ';';
    if (e.rttMs) stream_ << *e.rttMs;
    stream_ << ';';
    if (e.srttMs) stream_ << *e.srttMs;
    stream_ << ';' << StatusName(e.status) << ';' << double(e.observedAtUs - startUs_) / 1000.0 << '\n';
    stream_.flush();
}
}
