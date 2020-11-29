#include "J2534.hpp"

#include <libloaderapi.h>
#include <stdexcept>

namespace J2534
{
	template<typename FuncT>
	void LoadFunction(HINSTANCE hDLL, FuncT &func, const std::string& name)
	{
		func = reinterpret_cast<FuncT>(GetProcAddress(hDLL, name.c_str()));
		if (!func) {
			throw std::runtime_error("Can't load function: " + name);
		}
	}

	J2534::J2534(const std::wstring& path)
		: _hDLL{LoadLibrary(path.c_str())}
		, _deviceId{0}
	{
		if (!_hDLL) {
			throw std::runtime_error("Can't load library");
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
		LoadFunction(_hDLL, _PassThruSetProgrammingVoltage, "PassThruSetProgrammingVoltage");
		LoadFunction(_hDLL, _PassThruReadVersion, "PassThruReadVersion");
		LoadFunction(_hDLL, _PassThruGetLastError, "PassThruGetLastError");
		LoadFunction(_hDLL, _PassThruIoctl, "PassThruIoctl");
	}

	J2534::~J2534()
	{
		FreeLibrary(_hDLL);
	}

	J2534_ERROR_CODE J2534::PassThruOpen(const std::string& name)
	{
		return static_cast<J2534_ERROR_CODE>(_PassThruOpen(const_cast<char*>(name.c_str()), &_deviceId));
	}

	J2534_ERROR_CODE J2534::PassThruClose()
	{
		return static_cast<J2534_ERROR_CODE>(_PassThruClose(_deviceId));
	}

	J2534_ERROR_CODE J2534::PassThruConnect(unsigned long ProtocolID, unsigned long Flags, unsigned long Baudrate, unsigned long& channelID)
	{
		return static_cast<J2534_ERROR_CODE>(_PassThruConnect(_deviceId, ProtocolID, Flags, Baudrate, &channelID));
	}

	J2534_ERROR_CODE J2534::PassThruDisconnect(unsigned long ChannelID)
	{
		return static_cast<J2534_ERROR_CODE>(_PassThruDisconnect(ChannelID));
	}

	J2534_ERROR_CODE J2534::PassThruReadMsgs(unsigned long ChannelID, std::vector<PASSTHRU_MSG>& msgs, unsigned long Timeout)
	{
		unsigned long numMsgs{msgs.size()};
		const auto result = static_cast<J2534_ERROR_CODE>(_PassThruReadMsgs(ChannelID , msgs.data(), &numMsgs, Timeout));
		msgs.resize(numMsgs);
		return result;
	}

	J2534_ERROR_CODE J2534::PassThruWriteMsgs(unsigned long ChannelID, const std::vector<PASSTHRU_MSG>& msgs, unsigned long& numMsgs, unsigned long Timeout)
	{
		numMsgs = msgs.size();
		return static_cast<J2534_ERROR_CODE>(_PassThruWriteMsgs(ChannelID, const_cast<PASSTHRU_MSG*>(msgs.data()), &numMsgs, Timeout));
	}

	J2534_ERROR_CODE J2534::PassThruStartPeriodicMsg(unsigned long ChannelID, const PASSTHRU_MSG& msg,
		unsigned long& msgID, unsigned long TimeInterval)
	{
		return static_cast<J2534_ERROR_CODE>(_PassThruStartPeriodicMsg(ChannelID, const_cast<PASSTHRU_MSG *>(&msg), &msgID, TimeInterval));
	}

	J2534_ERROR_CODE J2534::PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID)
	{
		return static_cast<J2534_ERROR_CODE>(_PassThruStopPeriodicMsg(ChannelID, MsgID));
	}

	J2534_ERROR_CODE J2534::PassThruStartMsgFilter(unsigned long ChannelID,
		unsigned long FilterType, const PASSTHRU_MSG& maskMsg, const PASSTHRU_MSG& patternMsg,
		const PASSTHRU_MSG& flowControlMsg, unsigned long& msgID)
	{
		return static_cast<J2534_ERROR_CODE>(_PassThruStartMsgFilter(ChannelID, FilterType, const_cast<PASSTHRU_MSG*>(&maskMsg),
			const_cast<PASSTHRU_MSG*>(&patternMsg), const_cast<PASSTHRU_MSG*>(&flowControlMsg), &msgID));
	}

	J2534_ERROR_CODE J2534::PassThruStopMsgFilter(unsigned long ChannelID, unsigned long MsgID)
	{
		return static_cast<J2534_ERROR_CODE>(_PassThruStopMsgFilter(ChannelID, MsgID));
	}

	J2534_ERROR_CODE J2534::PassThruSetProgrammingVoltage(unsigned long Pin, unsigned long Voltage)
	{
		return static_cast<J2534_ERROR_CODE>(_PassThruSetProgrammingVoltage(_deviceId, Pin, Voltage));
	}

	J2534_ERROR_CODE J2534::PassThruReadVersion(std::string& firmwareVersion, std::string& dllVersion, std::string& apiVersion)
	{
		char *pFirmaweVersion = nullptr, *pDllVersion = nullptr, *pApiVersion = nullptr;
		const auto result = static_cast<J2534_ERROR_CODE>(_PassThruReadVersion(_deviceId, pFirmaweVersion, pDllVersion, pApiVersion));
		if (result == STATUS_NOERROR) {
			firmwareVersion = firmwareVersion;
			dllVersion = pDllVersion;
			apiVersion = pApiVersion;
		}
		return result;
	}

	J2534_ERROR_CODE J2534::PassThruGetLastError(std::string& errorDescription)
	{
		char* pErrorDescription = nullptr;
		const auto result = static_cast<J2534_ERROR_CODE>(_PassThruGetLastError(pErrorDescription));
		if (result == STATUS_NOERROR) {
			errorDescription = pErrorDescription;
		}
		return result;
	}

	J2534_ERROR_CODE J2534::PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID,
		const std::vector<uint8_t>& input, std::vector<uint8_t>& output)
	{
		return static_cast<J2534_ERROR_CODE>(_PassThruIoctl(ChannelID, IoctlID, const_cast<uint8_t*>(input.data()), output.data()));
	}

} // namespace J2534
