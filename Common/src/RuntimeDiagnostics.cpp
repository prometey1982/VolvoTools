#include "common/RuntimeDiagnostics.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winver.h>

#include <easylogging++.h>

#include <iostream>
#include <sstream>
#include <vector>

namespace common {

namespace {

constexpr FileVersion kMinimumMsvcRuntimeVersion{ 14, 40, 0, 0 };

std::string getLastWin32ErrorMessage()
{
    const auto error = GetLastError();
    if (error == ERROR_SUCCESS) {
        return {};
    }
    return "Win32 error " + std::to_string(error);
}

void printWarning(const std::string& message)
{
    LOG(WARNING) << message;
    std::cerr << "Warning: " << message << std::endl;
}

} // namespace

std::string getProcessArchitecture()
{
#if defined(_WIN64)
    return "x64";
#else
    return "x86";
#endif
}

std::string toString(const FileVersion& version)
{
    std::ostringstream ss;
    ss << version.major << '.' << version.minor << '.' << version.build << '.'
        << version.revision;
    return ss.str();
}

bool operator<(const FileVersion& lhs, const FileVersion& rhs)
{
    if (lhs.major != rhs.major) {
        return lhs.major < rhs.major;
    }
    if (lhs.minor != rhs.minor) {
        return lhs.minor < rhs.minor;
    }
    if (lhs.build != rhs.build) {
        return lhs.build < rhs.build;
    }
    return lhs.revision < rhs.revision;
}

bool getLoadedModuleVersion(const std::string& moduleName, FileVersion& version,
    std::string& modulePath)
{
    const auto module = GetModuleHandleA(moduleName.c_str());
    if (module == nullptr) {
        return false;
    }

    std::vector<char> path(MAX_PATH);
    const auto pathSize = GetModuleFileNameA(module, path.data(),
        static_cast<DWORD>(path.size()));
    if (pathSize == 0) {
        return false;
    }
    modulePath.assign(path.data(), pathSize);

    DWORD handle = 0;
    const auto versionInfoSize = GetFileVersionInfoSizeA(modulePath.c_str(), &handle);
    if (versionInfoSize == 0) {
        return false;
    }

    std::vector<uint8_t> versionInfo(versionInfoSize);
    if (!GetFileVersionInfoA(modulePath.c_str(), handle, versionInfoSize,
            versionInfo.data())) {
        return false;
    }

    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT fixedInfoSize = 0;
    if (!VerQueryValueA(versionInfo.data(), "\\",
            reinterpret_cast<void**>(&fixedInfo), &fixedInfoSize) ||
        fixedInfo == nullptr || fixedInfoSize < sizeof(VS_FIXEDFILEINFO)) {
        return false;
    }

    version.major = HIWORD(fixedInfo->dwFileVersionMS);
    version.minor = LOWORD(fixedInfo->dwFileVersionMS);
    version.build = HIWORD(fixedInfo->dwFileVersionLS);
    version.revision = LOWORD(fixedInfo->dwFileVersionLS);
    return true;
}

void printRuntimeDiagnostics(const std::string& appName)
{
    const auto arch = getProcessArchitecture();
    LOG(INFO) << appName << " process architecture=" << arch;

    FileVersion msvcVersion;
    std::string msvcPath;
    if (!getLoadedModuleVersion("MSVCP140.dll", msvcVersion, msvcPath)) {
        LOG(WARNING) << "Unable to read loaded MSVCP140.dll version: "
            << getLastWin32ErrorMessage();
        return;
    }

    LOG(INFO) << "Loaded MSVCP140.dll path=" << msvcPath
        << " version=" << toString(msvcVersion);
    if (msvcVersion < kMinimumMsvcRuntimeVersion) {
        printWarning("Loaded MSVCP140.dll version is " + toString(msvcVersion) +
            " (" + msvcPath + "). This " + arch +
            " build expects Microsoft Visual C++ Redistributable 2015-2022 " +
            arch + " version " + toString(kMinimumMsvcRuntimeVersion) +
            " or newer. Install the latest VC++ Redistributable " + arch +
            " if the application crashes in MSVCP140.dll.");
    }
}

void printJ2534ArchitectureHint(std::ostream& output)
{
    const auto arch = getProcessArchitecture();
    output << "Note: this is a " << arch << " process. It can see only " << arch
           << " J2534 drivers." << std::endl;
    output << "      32-bit J2534 drivers are not visible to x64 applications, "
              "and 64-bit drivers are not visible to x86 applications."
           << std::endl;
}

} // namespace common
