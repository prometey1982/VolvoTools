#include "J2534.hpp"

#include <stdexcept>

namespace j2534 {
template <typename FuncT>
void LoadFunction(HINSTANCE hDLL, FuncT &func, const std::string &name) {
  func = reinterpret_cast<FuncT>(GetProcAddress(hDLL, name.c_str()));
  if (!func) {
    throw std::runtime_error("Can't load function: " + name);
  }
}

J2534::J2534(const std::string &path)
    : _hDLL{LoadLibraryA(path.c_str())}, _deviceId{0}, _deviceOpened{false} {
  if (!_hDLL) {
    throw std::runtime_error("Can't load library '" + path + "'");
  }

  LoadFunction(_hDLL, _PassThruOpen, "PassThruOpen");
  LoadFunction(_hDLL, _PassThruClose, "PassThruClose");
  LoadFunction(_hDLL, _PassThruConnect, "PassThruConnect");
  LoadFunction(_hDLL, _PassThruDisconnect, "PassThruDisconnect");
  LoadFunction(_hDLL, _PassThruReadMsgs, "PassThruReadMsgs");
  LoadFunction(_hDLL, _PassThruWriteMsgs, "PassThruWriteMsgs");
  LoadFunction(_hDLL, _PassThruStartPeriodicMsg, "PassThruStartPeriodicMsg");
  LoadFunction(_hDLL, _PassThruStopPeriodicMsg, "PassThruStopPeriodicMsg");
  LoadFunction(_hDLL, _PassThruStartMsgFilter, "PassThruStartMsgFilter");
  LoadFunction(_hDLL, _PassThruStopMsgFilter, "PassThruStopMsgFilter");
  LoadFunction(_hDLL, _PassThruSetProgrammingVoltage,
               "PassThruSetProgrammingVoltage");
  LoadFunction(_hDLL, _PassThruReadVersion, "PassThruReadVersion");
  LoadFunction(_hDLL, _PassThruGetLastError, "PassThruGetLastError");
  LoadFunction(_hDLL, _PassThruIoctl, "PassThruIoctl");
}

J2534::~J2534() {
  if (_deviceOpened)
    PassThruClose();
  FreeLibrary(_hDLL);
}

J2534_ERROR_CODE J2534::PassThruOpen(const std::string &name) {
  const auto result = static_cast<J2534_ERROR_CODE>(
      _PassThruOpen(const_cast<char *>(name.c_str()), &_deviceId));
  _deviceOpened = (result == STATUS_NOERROR);
  return result;
}

J2534_ERROR_CODE J2534::PassThruClose() {
  const auto result = static_cast<J2534_ERROR_CODE>(_PassThruClose(_deviceId));
  _deviceOpened = !(result == STATUS_NOERROR);
  return result;
}

J2534_ERROR_CODE J2534::PassThruConnect(unsigned long ProtocolID,
                                        unsigned long Flags,
                                        unsigned long Baudrate,
                                        unsigned long &channelID) {
  return static_cast<J2534_ERROR_CODE>(
      _PassThruConnect(_deviceId, ProtocolID, Flags, Baudrate, &channelID));
}

J2534_ERROR_CODE J2534::PassThruDisconnect(unsigned long ChannelID) {
  return static_cast<J2534_ERROR_CODE>(_PassThruDisconnect(ChannelID));
}

J2534_ERROR_CODE J2534::PassThruReadMsgs(unsigned long ChannelID,
                                         std::vector<PASSTHRU_MSG> &msgs,
                                         unsigned long Timeout) const {
  unsigned long numMsgs{static_cast<unsigned long>(msgs.size())};
  const auto result = static_cast<J2534_ERROR_CODE>(
      _PassThruReadMsgs(ChannelID, msgs.data(), &numMsgs, Timeout));
  msgs.resize(numMsgs);
  return result;
}

J2534_ERROR_CODE J2534::PassThruWriteMsgs(unsigned long ChannelID,
                                          const std::vector<PASSTHRU_MSG> &msgs,
                                          unsigned long &numMsgs,
                                          unsigned long Timeout) const {
  numMsgs = msgs.size();
  return static_cast<J2534_ERROR_CODE>(_PassThruWriteMsgs(
      ChannelID, const_cast<PASSTHRU_MSG *>(msgs.data()), &numMsgs, Timeout));
}

J2534_ERROR_CODE
J2534::PassThruStartPeriodicMsg(unsigned long ChannelID,
                                const PASSTHRU_MSG &msg, unsigned long &msgID,
                                unsigned long TimeInterval) const {
  return static_cast<J2534_ERROR_CODE>(_PassThruStartPeriodicMsg(
      ChannelID, const_cast<PASSTHRU_MSG *>(&msg), &msgID, TimeInterval));
}

J2534_ERROR_CODE J2534::PassThruStopPeriodicMsg(unsigned long ChannelID,
                                                unsigned long MsgID) const {
  return static_cast<J2534_ERROR_CODE>(
      _PassThruStopPeriodicMsg(ChannelID, MsgID));
}

J2534_ERROR_CODE J2534::PassThruStartMsgFilter(unsigned long ChannelID,
                                               unsigned long FilterType,
                                               PASSTHRU_MSG *maskMsg,
                                               PASSTHRU_MSG *patternMsg,
                                               PASSTHRU_MSG *flowControlMsg,
                                               unsigned long &msgID) const {
  return static_cast<J2534_ERROR_CODE>(_PassThruStartMsgFilter(
      ChannelID, FilterType, maskMsg, patternMsg, flowControlMsg, &msgID));
}

J2534_ERROR_CODE J2534::PassThruStopMsgFilter(unsigned long ChannelID,
                                              unsigned long MsgID) const {
  return static_cast<J2534_ERROR_CODE>(
      _PassThruStopMsgFilter(ChannelID, MsgID));
}

J2534_ERROR_CODE
J2534::PassThruSetProgrammingVoltage(unsigned long Pin,
                                     unsigned long Voltage) const {
  return static_cast<J2534_ERROR_CODE>(
      _PassThruSetProgrammingVoltage(_deviceId, Pin, Voltage));
}

J2534_ERROR_CODE J2534::PassThruReadVersion(std::string &firmwareVersion,
                                            std::string &dllVersion,
                                            std::string &apiVersion) const {
  char *pFirmaweVersion = nullptr, *pDllVersion = nullptr,
       *pApiVersion = nullptr;
  const auto result = static_cast<J2534_ERROR_CODE>(_PassThruReadVersion(
      _deviceId, pFirmaweVersion, pDllVersion, pApiVersion));
  if (result == STATUS_NOERROR) {
    firmwareVersion = firmwareVersion;
    dllVersion = pDllVersion;
    apiVersion = pApiVersion;
  }
  return result;
}

J2534_ERROR_CODE
J2534::PassThruGetLastError(std::string &errorDescription) const {
  char *pErrorDescription = nullptr;
  const auto result =
      static_cast<J2534_ERROR_CODE>(_PassThruGetLastError(pErrorDescription));
  if (result == STATUS_NOERROR) {
    errorDescription = pErrorDescription;
  }
  return result;
}

J2534_ERROR_CODE J2534::PassThruIoctl(unsigned long ChannelID,
                                      unsigned long IoctlID, const void *input,
                                      void *output) const {
  return static_cast<J2534_ERROR_CODE>(
      _PassThruIoctl(ChannelID, IoctlID, const_cast<void *>(input), output));
}

} // namespace j2534
