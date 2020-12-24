#include "Flasher.hpp"

#include "../common/CEMCanMessage.hpp"
#include "../j2534/J2534.hpp"
#include "../j2534/J2534Channel.hpp"

#include <algorithm>

namespace flasher {
Flasher::Flasher(j2534::J2534 &j2534) : _j2534{j2534} {}

Flasher::~Flasher() { stop(); }

void Flasher::registerCallback(FlasherCallback &callback) {
  std::unique_lock<std::mutex> lock{_mutex};
  _callbacks.push_back(&callback);
}

void Flasher::unregisterCallback(FlasherCallback &callback) {
  std::unique_lock<std::mutex> lock{_mutex};
  _callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
                   _callbacks.end());
}

void Flasher::flash(unsigned long baudrate, const std::vector<uint8_t> &bin) {
//    _thre
}

void Flasher::stop() {}

void Flasher::flasherFunction(const std::vector<uint8_t> bin) {}
} // namespace flasher
