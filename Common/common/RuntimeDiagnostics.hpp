#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace common {

struct FileVersion {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t build = 0;
    uint16_t revision = 0;
};

std::string getProcessArchitecture();
std::string toString(const FileVersion& version);
bool operator<(const FileVersion& lhs, const FileVersion& rhs);

bool getLoadedModuleVersion(const std::string& moduleName, FileVersion& version,
    std::string& modulePath);

void printRuntimeDiagnostics(const std::string& appName);
void printJ2534ArchitectureHint(std::ostream& output);

} // namespace common
