#include "../Common/Util.hpp"
#include "../j2534/J2534.hpp"
#include "LogParameters.hpp"
#include "Logger.h"
#include "LoggerApplication.hpp"

#include <boost/program_options.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <thread>

class FileLogWriter final : public logger::LoggerCallback {
public:
  FileLogWriter(const std::string &outputPath,
                const logger::LogParameters &parameters)
      : _outputStream{outputPath} {
    _outputStream << "Time (sec),";
    const auto startTimepoint{std::chrono::steady_clock::now()};
    unsigned long numberOfCanMessages{0};
    numberOfCanMessages = parameters.getNumberOfCanMessages();
    for (const auto &param : parameters.parameters()) {
      _outputStream << param.name() << "(" << param.unit() << "),";
    }
    _outputStream << std::endl;
  }

  void OnLogMessage(std::chrono::milliseconds timePoint,
                    const std::vector<double> &values) {
    _outputStream << (timePoint.count() / 1000.0) << ",";

    for (const auto value : values) {
      _outputStream << value << ",";
    }
    _outputStream << std::endl;
  }

private:
  std::ofstream _outputStream;
};

class ConsoleLogWriter final : public logger::LoggerCallback {
public:
  explicit ConsoleLogWriter(size_t printLimit) : _printLimit{printLimit} {}

  void OnLogMessage(std::chrono::milliseconds timePoint,
                    const std::vector<double> &values) {
    std::cout << (timePoint.count() / 1000.0) << ",";

    for (size_t i = 0; i < _printLimit && i < values.size(); ++i) {
      std::cout << std::setprecision(2) << values[i] << ",";
    }
    std::cout << std::endl;
  }

private:
  const size_t _printLimit;
};

bool getRunOptions(int argc, const char *argv[], unsigned long &baudrate,
                   std::string &paramsFilePath, std::string &outputPath) {
  using namespace boost::program_options;
  options_description descr;
  descr.add_options()(
      "baudrate,b", value<unsigned long>()->default_value(500000),
      "CAN bus speed")("variables,v", value<std::string>()->required(),
                       "Path to memory variables")(
      "output,o", value<std::string>()->required(), "Path to save logs");
  command_line_parser parser{argc, argv};
  parser.options(descr);
  variables_map vm;
  store(parser.run(), vm);
  if (vm.count("variables") && vm.count("output")) {
    baudrate = vm["baudrate"].as<unsigned long>();
    paramsFilePath = vm["variables"].as<std::string>();
    outputPath = vm["output"].as<std::string>();
    return true;
  } else {
    std::cout << descr;
    return false;
  }
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
  std::string paramsFilePath;
  std::string outputPath;
  if (getRunOptions(argc, argv, baudrate, paramsFilePath, outputPath)) {
    const auto libraryParams{common::getLibraryParams()};
    if (!libraryParams.first.empty()) {
      try {
        std::unique_ptr<j2534::J2534> j2534{
            std::make_unique<j2534::J2534>(libraryParams.first)};
        j2534->PassThruOpen(libraryParams.second);
        logger::LogParameters params{paramsFilePath};
        FileLogWriter fileLogWriter(outputPath, params);
        ConsoleLogWriter consoleLogWriter{5};
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
  return 0;
}
