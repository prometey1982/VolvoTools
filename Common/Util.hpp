#pragma once

#include <string>
#include <utility>

namespace common {

std::wstring toWstring(const std::string &str);
std::string toString(const std::wstring &str);

std::pair<std::string, std::string> getLibraryParams();

} // namespace common
