// VolvoLogger.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "J2534.hpp"
#include "LogParameters.hpp"
#include "Logger.h"

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
    std::string paramFilePath{ "C:\\misc\\programming\\VolvoLogger\\example.ecu" };
    logger::LogParameters params{ paramFilePath };
    std::wstring libraryPath;
    std::string deviceName;
    const std::wstring rootKeyName{ L"Software\\Wow6432Node\\PassThruSupport.04.04" };
    const auto key = LocalMachine->Open(rootKeyName);
    key->EnumerateSubKeys([&rootKeyName, &libraryPath, &deviceName](const auto& subKeyName) {
        return ProcessRegistry(rootKeyName + L"\\" + subKeyName, libraryPath, deviceName);
    });
    if (!libraryPath.empty()) {
        j2534::J2534 j2534(libraryPath);
        j2534.PassThruOpen(deviceName);
        logger::Logger logger(j2534);
        logger.start(params, L"123.csv");
        getchar();
        logger.stop();
        j2534.PassThruClose();
    }
    return 0;
}
