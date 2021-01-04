#include "../Common/CanMessages.hpp"
#include "../Common/Util.hpp"
#include "../j2534/J2534.hpp"
#include "Flasher.hpp"

#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <iterator>

bool getRunOptions(int argc, const char *argv[], unsigned long &baudrate,
                   std::string &flashPath) {
  using namespace boost::program_options;
  options_description descr;
  descr.add_options()("baudrate,b",
                      value<unsigned long>()->default_value(500000),
                      "CAN bus speed")(
      "flash,f", value<std::string>()->required(), "Path to flash BIN");
  command_line_parser parser{argc, argv};
  parser.options(descr);
  variables_map vm;
  store(parser.run(), vm);
  if (vm.count("flash")) {
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

int main(int argc, const char *argv[]) {
  if (!SetConsoleCtrlHandler(HandlerRoutine, TRUE)) {
    throw std::runtime_error("Can't set console control hander");
  }
  unsigned long baudrate = 0;
  std::string flashPath;
  if (getRunOptions(argc, argv, baudrate, flashPath)) {
    const auto libraryParams{common::getLibraryParams()};
    if (!libraryParams.first.empty()) {
      try {
        std::fstream input(flashPath,
                           std::ios_base::binary | std::ios_base::in);
        std::vector<uint8_t> bin{std::istreambuf_iterator<char>(input), {}};

        std::unique_ptr<j2534::J2534> j2534{
            std::make_unique<j2534::J2534>(libraryParams.first)};
        j2534->PassThruOpen(libraryParams.second);
        flasher::Flasher flasher(*j2534);
        flasher.flash(baudrate, bin);
        while (flasher.getState() == flasher::Flasher::State::InProgress) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
          std::cout << "*";
        }
        std::cout << std::endl
                  << ((flasher.getState() == flasher::Flasher::State::Done)
                          ? "Flashing done"
                          : "Flashing error. Try again")
                  << std::endl;
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
