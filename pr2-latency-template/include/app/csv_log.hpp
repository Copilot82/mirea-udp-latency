#pragma once
#include "telemetry/telemetry.hpp"
#include <cstdint>
#include <fstream>
#include <string>

namespace app {
class CsvLog {
public:
    CsvLog(const std::string& path, std::string experiment, std::uint64_t startUs);
    void Write(const MeasurementEvent& event);
private:
    std::ofstream stream_;
    std::string experiment_;
    std::uint64_t startUs_;
};
}
