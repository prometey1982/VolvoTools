#pragma once

#include "J2534_v0404.h"

#include <string>
#include <vector>

namespace J2534
{
	class J2534 final
	{
	public:
		explicit J2534(const std::wstring& path);
		~J2534();

		J2534_ERROR_CODE PassThruOpen(const std::string& name);
		J2534_ERROR_CODE PassThruClose();
		J2534_ERROR_CODE PassThruConnect(unsigned long ProtocolID, unsigned long Flags, unsigned long Baudrate, unsigned long& channelID);
		J2534_ERROR_CODE PassThruDisconnect(unsigned long ChannelID);
		J2534_ERROR_CODE PassThruReadMsgs(unsigned long ChannelID, std::vector<PASSTHRU_MSG>& msgs, unsigned long Timeout);
		J2534_ERROR_CODE PassThruWriteMsgs(unsigned long ChannelID, const std::vector<PASSTHRU_MSG>& msgs, unsigned long& numMsgs, unsigned long Timeout);
		J2534_ERROR_CODE PassThruStartPeriodicMsg(unsigned long ChannelID, const PASSTHRU_MSG& msg,
			unsigned long& msgID, unsigned long TimeInterval);
		J2534_ERROR_CODE PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID);
		J2534_ERROR_CODE PassThruStartMsgFilter(unsigned long ChannelID,
			unsigned long FilterType, const PASSTHRU_MSG& maskMsg, const PASSTHRU_MSG& patternMsg,
			const PASSTHRU_MSG& flowControlMsg, unsigned long& msgID);
		J2534_ERROR_CODE PassThruStopMsgFilter(unsigned long ChannelID, unsigned long MsgID);
		J2534_ERROR_CODE PassThruSetProgrammingVoltage(unsigned long Pin, unsigned long Voltage);
		J2534_ERROR_CODE PassThruReadVersion(std::string& firmwareVersion, std::string& dllVersion, std::string& apiVersion);
		J2534_ERROR_CODE PassThruGetLastError(std::string& errorDescription);
		J2534_ERROR_CODE PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID,
			const std::vector<uint8_t>& input, std::vector<uint8_t>& output);

	private:
		HINSTANCE _hDLL;
		unsigned long _deviceId;

		PTOPEN _PassThruOpen;
		PTCLOSE _PassThruClose;
		PTCONNECT _PassThruConnect;
		PTDISCONNECT _PassThruDisconnect;
		PTREADMSGS _PassThruReadMsgs;
		PTWRITEMSGS _PassThruWriteMsgs;
		PTSTARTPERIODICMSG _PassThruStartPeriodicMsg;
		PTSTOPPERIODICMSG _PassThruStopPeriodicMsg;
		PTSTARTMSGFILTER _PassThruStartMsgFilter;
		PTSTOPMSGFILTER _PassThruStopMsgFilter;
		PTSETPROGRAMMINGVOLTAGE _PassThruSetProgrammingVoltage;
		PTREADVERSION _PassThruReadVersion;
		PTGETLASTERROR _PassThruGetLastError;
		PTIOCTL _PassThruIoctl;
	};
} // namespace J2534
