#include "../Common/Util.hpp"
#include "../j2534/J2534.hpp"
#include "FileLogWriter.hpp"
#include "LogParameters.hpp"
#include "Logger.hpp"
#include "LoggerApplication.hpp"
#include "LoggerCallback.hpp"

#include <boost/program_options.hpp>
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

bool getRunOptions(int argc, const char *argv[], unsigned long &baudrate,
                   std::string &paramsFilePath, std::string &outputPath,
                   unsigned &printCount) {
  using namespace boost::program_options;
  options_description descr;
  descr.add_options()("baudrate,b",
                      value<unsigned long>()->default_value(500000),
                      "CAN bus speed")(
      "variables,v", value<std::string>()->required(),
      "Path to memory variables")("output,o", value<std::string>()->required(),
                                  "Path to save logs")("print,p",
                                                       value<unsigned>()
                                                           ->default_value(5),
                                                       "Number of variables "
                                                       "which prints to "
                                                       "console");
  command_line_parser parser{argc, argv};
  parser.options(descr);
  variables_map vm;
  store(parser.run(), vm);
  if (vm.count("variables") && vm.count("output")) {
    baudrate = vm["baudrate"].as<unsigned long>();
    paramsFilePath = vm["variables"].as<std::string>();
    outputPath = vm["output"].as<std::string>();
    printCount = vm["print"].as<unsigned>();
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
  unsigned printCount;
  if (getRunOptions(argc, argv, baudrate, paramsFilePath, outputPath,
                    printCount)) {
    const auto libraryParams{common::getLibraryParams()};
    for (const auto &param : libraryParams) {
      if (!param.first.empty()) {
        try {
          std::unique_ptr<j2534::J2534> j2534{
              std::make_unique<j2534::J2534>(param.first)};
          j2534->PassThruOpen(param.second);
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
  }
  return 0;
}
