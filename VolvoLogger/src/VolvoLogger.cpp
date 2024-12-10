#include "FileLogWriter.hpp"
#include "LoggerApplication.hpp"

#include <logger/LogParameters.hpp>
#include <logger/Logger.hpp>
#include <logger/LoggerCallback.hpp>
#include <common/Util.hpp>
#include <j2534/J2534.hpp>

#include <argparse/argparse.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <thread>

class ConsoleLogWriter final : public logger::LoggerCallback {
public:
  explicit ConsoleLogWriter(size_t printLimit) : _printLimit{printLimit} {}

  void onLogMessage(std::chrono::milliseconds timePoint,
                    const std::vector<double> &values) override {
    std::cout << (timePoint.count() / 1000.0) << ",";

    for (size_t i = 0; i < _printLimit && i < values.size(); ++i) {
      std::cout << std::setprecision(2) << values[i] << ",";
    }
    std::cout << std::endl;
  }

  void onStatusChanged(bool /*started*/) override {}

private:
  const size_t _printLimit;
};

bool getRunOptions(int argc, const char *argv[], std::string &deviceName,
                   unsigned long &baudrate, std::string &paramsFilePath,
                   std::string &outputPath, unsigned &printCount) {
  argparse::ArgumentParser program("VolvoLogger");
  program.add_argument("-d", "--device").default_value(std::string{}).help("Device name");
  program.add_argument("-b", "--baudrate").scan<'u', unsigned>().default_value(500000u).help("CAN bus speed");
  program.add_argument("-v", "--variables").help("Path to memory variables");
  program.add_argument("-o", "--output").help("Path to save logs");
  program.add_argument("-p", "--print").scan<'u', unsigned>().default_value(5u).help("Number of variables which prints to console");

  try {
      program.parse_args(argc, argv);
      deviceName = program.get<std::string>("-d");
      baudrate = program.get<unsigned>("-b");
      paramsFilePath = program.get<std::string>("-v");
      outputPath = program.get<std::string>("-o");
      printCount = program.get<unsigned>("-p");
      return true;
  }
  catch (const std::exception& err) {
      std::cerr << err.what() << std::endl;
      std::cerr << program;
  }
  return false;
}

BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType) {
  logger::LoggerApplication::instance().stop();
  return TRUE;
}

int main(int argc, const char *argv[]) {
  if (!SetConsoleCtrlHandler(HandlerRoutine, TRUE)) {
    throw std::runtime_error("Can't set console control hander");
  }
  unsigned long baudrate = 0;
  std::string deviceName;
  std::string paramsFilePath;
  std::string outputPath;
  unsigned printCount;
  const auto devices = common::getAvailableDevices();
  if (getRunOptions(argc, argv, deviceName, baudrate, paramsFilePath,
                    outputPath, printCount)) {
    for (const auto &device : devices) {
      if (deviceName.empty() ||
          device.deviceName.find(deviceName) != std::string::npos) {
        try {
          std::unique_ptr<j2534::J2534> j2534{
              std::make_unique<j2534::J2534>(device.libraryName)};
          std::string name =
              device.deviceName.find("DiCE-") != std::string::npos
                  ? device.deviceName
                  : "";
          j2534->PassThruOpen(name);
          logger::LogParameters params{paramsFilePath};
          logger::FileLogWriter fileLogWriter(outputPath, params);
          ConsoleLogWriter consoleLogWriter{printCount};
          logger::LoggerApplication::instance().start(
              baudrate, std::move(j2534), params,
              {&fileLogWriter, &consoleLogWriter});
          while (logger::LoggerApplication::instance().isStarted()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
          }
          logger::LoggerApplication::instance().stop();
        } catch (const std::exception &ex) {
          std::cout << ex.what() << std::endl;
        } catch (const char *ex) {
          std::cout << ex << std::endl;
        } catch (...) {
          std::cout << "exception" << std::endl;
        }
      }
    }
  } else {
    std::cout << "Available J2534 devices:" << std::endl;
    for (const auto &device : devices) {
      std::cout << "    " << device.deviceName << std::endl;
    }
  }
  return 0;
}
