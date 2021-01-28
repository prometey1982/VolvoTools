#include "J2534Channel.hpp"

#include <stdexcept>

namespace j2534 {

J2534Channel::J2534Channel(J2534 &j2534, unsigned long ProtocolID,
                           unsigned long Flags, unsigned long Baudrate)
    : _j2534{j2534} {
  if (_j2534.PassThruConnect(ProtocolID, Flags, Baudrate, _channelID) !=
      STATUS_NOERROR) {
    throw std::runtime_error("Can't open channel");
  }
}

J2534Channel::~J2534Channel() { _j2534.PassThruDisconnect(_channelID); }

J2534_ERROR_CODE J2534Channel::readMsgs(std::vector<PASSTHRU_MSG> &msgs,
                                        unsigned long Timeout) const {
  return _j2534.PassThruReadMsgs(_channelID, msgs, Timeout);
}

J2534_ERROR_CODE J2534Channel::writeMsgs(const std::vector<PASSTHRU_MSG> &msgs,
                                         unsigned long &numMsgs,
                                         unsigned long Timeout) const {
  return _j2534.PassThruWriteMsgs(_channelID, msgs, numMsgs, Timeout);
}

J2534_ERROR_CODE
J2534Channel::startPeriodicMsg(const PASSTHRU_MSG &msg, unsigned long &msgID,
                               unsigned long TimeInterval) const {
  return _j2534.PassThruStartPeriodicMsg(_channelID, msg, msgID, TimeInterval);
}

J2534_ERROR_CODE J2534Channel::stopPeriodicMsg(unsigned long MsgID) const {
  return _j2534.PassThruStopPeriodicMsg(_channelID, MsgID);
}

J2534_ERROR_CODE J2534Channel::startMsgFilter(unsigned long FilterType,
                                              PASSTHRU_MSG *maskMsg,
                                              PASSTHRU_MSG *patternMsg,
                                              PASSTHRU_MSG *flowControlMsg,
                                              unsigned long &msgID) const {
  return _j2534.PassThruStartMsgFilter(_channelID, FilterType, maskMsg,
                                       patternMsg, flowControlMsg, msgID);
}

J2534_ERROR_CODE J2534Channel::stopMsgFilter(unsigned long MsgID) const {
  return _j2534.PassThruStopMsgFilter(_channelID, MsgID);
}

J2534_ERROR_CODE J2534Channel::passThruIoctl(unsigned long IoctlID,
                                             const void *input,
                                             void *output) const {
  return _j2534.PassThruIoctl(_channelID, IoctlID, input, output);
}

J2534_ERROR_CODE J2534Channel::clearRx() const {
  return passThruIoctl(CLEAR_RX_BUFFER, nullptr, nullptr);
}

J2534_ERROR_CODE J2534Channel::clearTx() const {
  return passThruIoctl(CLEAR_TX_BUFFER, nullptr, nullptr);
}

J2534_ERROR_CODE
J2534Channel::setConfig(const std::vector<SCONFIG> &config) const {
  SCONFIG_LIST configList;
  configList.NumOfParams = config.size();
  configList.ConfigPtr = const_cast<SCONFIG *>(config.data());
  return passThruIoctl(SET_CONFIG, &configList);
}

} // namespace j2534
