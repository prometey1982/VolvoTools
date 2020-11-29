#pragma once

#include "J2534.hpp"

namespace J2534
{
	class J2534Channel final
	{
	public:
		explicit J2534Channel(J2534& j2534, unsigned long ProtocolID, unsigned long Flags, unsigned long Baudrate);
		~J2534Channel();

		J2534_ERROR_CODE readMsgs(std::vector<PASSTHRU_MSG> msgs, unsigned long Timeout);
		J2534_ERROR_CODE writeMsgs(const std::vector<PASSTHRU_MSG> msgs, unsigned long& numMsgs, unsigned long Timeout);
		J2534_ERROR_CODE startPeriodicMsg(const PASSTHRU_MSG& msg, unsigned long& msgID, unsigned long TimeInterval);
		J2534_ERROR_CODE stopPeriodicMsg(unsigned long MsgID);
		J2534_ERROR_CODE startMsgFilter(unsigned long FilterType, const PASSTHRU_MSG& maskMsg, const PASSTHRU_MSG& patternMsg,
			const PASSTHRU_MSG& flowControlMsg, unsigned long& msgID);
		J2534_ERROR_CODE stopMsgFilter(unsigned long MsgID);
		J2534_ERROR_CODE passThruIoctl(unsigned long IoctlID, const std::vector<uint8_t>& input, std::vector<uint8_t>& output);

	private:
		J2534& _j2534;
		unsigned long _channelID;
	};
} // namespace J2534
