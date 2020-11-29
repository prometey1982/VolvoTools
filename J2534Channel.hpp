#pragma once

#include "J2534.hpp"

namespace j2534
{
	class J2534Channel final
	{
	public:
		explicit J2534Channel(J2534& j2534, unsigned long ProtocolID, unsigned long Flags, unsigned long Baudrate);
		~J2534Channel();

		J2534_ERROR_CODE readMsgs(std::vector<PASSTHRU_MSG> msgs, unsigned long Timeout) const;
		J2534_ERROR_CODE writeMsgs(const std::vector<PASSTHRU_MSG> msgs, unsigned long& numMsgs, unsigned long Timeout) const;
		J2534_ERROR_CODE startPeriodicMsg(const PASSTHRU_MSG& msg, unsigned long& msgID, unsigned long TimeInterval) const;
		J2534_ERROR_CODE stopPeriodicMsg(unsigned long MsgID) const;
		J2534_ERROR_CODE startMsgFilter(unsigned long FilterType, PASSTHRU_MSG* maskMsg, PASSTHRU_MSG* patternMsg,
			PASSTHRU_MSG* flowControlMsg, unsigned long& msgID) const;
		J2534_ERROR_CODE stopMsgFilter(unsigned long MsgID) const;
		J2534_ERROR_CODE passThruIoctl(unsigned long IoctlID, const void* input, void* output = nullptr) const;

		J2534_ERROR_CODE setConfig(const std::vector<SCONFIG>& config) const;

	private:
		J2534& _j2534;
		unsigned long _channelID;
	};
} // namespace j2534
