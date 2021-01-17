#include "../Common/CanMessages.hpp"
#include "../Common/Util.hpp"
#include "../j2534/J2534.hpp"
#include "Flasher.hpp"

#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <iterator>

bool getRunOptions(int argc, const char *argv[], unsigned long &baudrate,
                   std::string &flashPath, bool &wakeup) {
  wakeup = false;
  using namespace boost::program_options;
  options_description descr;
  descr.add_options()(
      "baudrate,b", value<unsigned long>()->default_value(500000),
      "CAN bus speed")("flash,f", value<std::string>(),
                       "Path to flash BIN")("wakeup,w", "Wake up CAN network");
  command_line_parser parser{argc, argv};
  parser.options(descr);
  variables_map vm;
  store(parser.run(), vm);
  if (vm.count("wakeup")) {
    baudrate = vm["baudrate"].as<unsigned long>();
    wakeup = true;
    return true;
  } else if (vm.count("flash")) {
    baudrate = vm["baudrate"].as<unsigned long>();
    flashPath = vm["flash"].as<std::string>();
    return true;
  } else {
    std::cout << descr;
    return false;
  }
}

BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType) {
  //    logger::LoggerApplication::instance().stop();
  return TRUE;
}

class FlasherCallback final : public flasher::FlasherCallback {
public:
  FlasherCallback() = default;

  void OnFlashProgress(std::chrono::milliseconds timePoint, size_t currentValue,
                       size_t maxValue) override {}
  void OnMessage(const std::string &message) override {
    std::cout << std::endl << message;
  }
};

void writeBinToFile(const std::vector<uint8_t> &bin, const std::string &path) {
  std::fstream out(path, std::ios::out | std::ios::binary);
  const auto msg = common::CanMessages::createWriteDataMsgs(bin);
  const auto passThruMsgs = msg.toPassThruMsgs(123, 456);
  for (const auto &msg : passThruMsgs) {
    for (size_t i = 0; i < msg.DataSize; ++i)
      out << msg.Data[i];
  }
}

int main(int argc, const char *argv[]) {
  if (!SetConsoleCtrlHandler(HandlerRoutine, TRUE)) {
    throw std::runtime_error("Can't set console control hander");
  }
  // const auto msg = common::CanMessages::createWriteOffsetMsg(0x31C000);
  // const auto now{ std::chrono::system_clock::now() };
  // const auto time_t = std::chrono::system_clock::to_time_t(now);
  // struct tm lt;
  // localtime_s(&lt, &time_t);
  // const auto msg = common::CanMessages::setCurrentTime(lt.tm_hour,
  // lt.tm_min); writeBinToFile(common::CanMessages::me7BootLoader,
  // "c:\\misc\\prometey_bootloader.bin"); std::fstream
  // input("c:\\misc\\test.bin", std::ios::in | std::ios::binary);
  // std::vector<uint8_t> bin{ std::istreambuf_iterator<char>(input), {} };
  // writeBinToFile(bin, "c:\\misc\\prometey_bin.bin");
  // return 0;

  unsigned long baudrate = 0;
  std::string flashPath;
  bool wakeup = false;
  if (getRunOptions(argc, argv, baudrate, flashPath, wakeup)) {
    const auto libraryParams{common::getLibraryParams()};
    if (!libraryParams.first.empty()) {
      try {
        if (wakeup) {
          std::unique_ptr<j2534::J2534> j2534{
              std::make_unique<j2534::J2534>(libraryParams.first)};
          j2534->PassThruOpen(libraryParams.second);
          flasher::Flasher flasher(*j2534);
          flasher.canWakeUp(baudrate);
        } else {
          std::fstream input(flashPath,
                             std::ios_base::binary | std::ios_base::in);
          std::vector<uint8_t> bin{std::istreambuf_iterator<char>(input), {}};

          std::unique_ptr<j2534::J2534> j2534{
              std::make_unique<j2534::J2534>(libraryParams.first)};
          j2534->PassThruOpen(libraryParams.second);
          FlasherCallback callback;
          flasher::Flasher flasher(*j2534);
          flasher.registerCallback(callback);
          flasher.flash(baudrate, bin);
          while (flasher.getState() == flasher::Flasher::State::InProgress) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << ".";
          }
          std::cout << std::endl
                    << ((flasher.getState() == flasher::Flasher::State::Done)
                            ? "Flashing done"
                            : "Flashing error. Try again.")
                    << std::endl;
        }
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
