// VolvoLogger.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "J2534.hpp"

#include <iostream>
#include <Registry.hpp>
#include <locale>
#include <codecvt>

using namespace m4x1m1l14n::Registry;

bool ProcessRegistry(const std::wstring& keyName, std::wstring& libraryPath, std::string& deviceName)
{
    const auto key = LocalMachine->Open(keyName);
    const auto canXonXoff{ key->GetInt32(L"CAN_XON_XOFF") };
    if (canXonXoff > 0) {
        libraryPath = key->GetString(L"FunctionLibrary");
        using convert_type = std::codecvt_utf8<wchar_t>;
        std::wstring_convert<convert_type, wchar_t> converter;
        deviceName = converter.to_bytes(key->GetString(L"Name"));
        return false;
    }
    return true;
}

int main()
{
    std::wstring libraryPath;
    std::string deviceName;
    const std::wstring rootKeyName{ L"Software\\PassThruSupport.04.04" };
    const auto key = LocalMachine->Open(rootKeyName);
    key->EnumerateSubKeys([&rootKeyName, &libraryPath, &deviceName](const auto& subKeyName) {
        return ProcessRegistry(rootKeyName + L"\\" + subKeyName, libraryPath, deviceName);
    });
    if (!libraryPath.empty()) {
        J2534::J2534 j2534(libraryPath);
        j2534.PassThruOpen(deviceName);
    }
    return 0;
}
