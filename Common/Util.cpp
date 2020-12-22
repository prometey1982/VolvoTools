#include "Util.hpp"

#include <codecvt>
#include <locale>

#include "../Registry/Registry/include/Registry.hpp"

namespace common {

std::wstring toWstring(const std::string &str) {
  using convert_type = std::codecvt_utf8<wchar_t>;
  std::wstring_convert<convert_type, wchar_t> converter;
  return converter.from_bytes(str);
}

std::string toString(const std::wstring &str) {
  using convert_type = std::codecvt_utf8<wchar_t>;
  std::wstring_convert<convert_type, wchar_t> converter;
  return converter.to_bytes(str);
}

using namespace m4x1m1l14n::Registry;

static bool processRegistry(const std::string &keyName,
                            std::string &libraryPath, std::string &deviceName) {
  const auto key = LocalMachine->Open(keyName);
  const auto canXonXoff{key->GetInt32(TEXT("CAN_XON_XOFF"))};
  if (canXonXoff > 0) {
    libraryPath = key->GetString(TEXT("FunctionLibrary"));
    deviceName = key->GetString(TEXT("Name"));
    return false;
  }
  return true;
}

std::pair<std::string, std::string> getLibraryParams() {
  std::string libraryPath;
  std::string deviceName;
  const std::string rootKeyName{TEXT("Software\\PassThruSupport.04.04")};
  const auto key = LocalMachine->Open(rootKeyName);
  key->EnumerateSubKeys(
      [&rootKeyName, &libraryPath, &deviceName](const auto &subKeyName) {
        return processRegistry(rootKeyName + TEXT("\\") + subKeyName,
                               libraryPath, deviceName);
      });
  return {libraryPath, deviceName};
}

} // namespace common
