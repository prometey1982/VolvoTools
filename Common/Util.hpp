#pragma once

#include <memory>
#include <string>
#include <utility>

namespace j2534 {
class J2534;
class J2534Channel;
} // namespace j2534

namespace common {

std::wstring toWstring(const std::string &str);
std::string toString(const std::wstring &str);

#ifdef UNICODE
std::wstring toPlatformString(const std::string &str);
std::string fromPlatformString(const std::wstring &str);
#else
std::string toPlatformString(const std::string &str);
std::string fromPlatformString(const std::string &str);
#endif

std::pair<std::string, std::string> getLibraryParams();

std::unique_ptr<j2534::J2534Channel> openChannel(j2534::J2534 &j2534,
                                                 unsigned long ProtocolID,
                                                 unsigned long Flags,
                                                 unsigned long Baudrate);

std::unique_ptr<j2534::J2534Channel> openBridgeChannel(j2534::J2534 &j2534);

} // namespace common
