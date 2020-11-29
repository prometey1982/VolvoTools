#include "J2534Channel.hpp"

#include <stdexcept>

namespace J2534
{

	J2534Channel::J2534Channel(J2534& j2534, unsigned long ProtocolID, unsigned long Flags, unsigned long Baudrate)
		: _j2534{j2534}
	{
		if (_j2534.PassThruConnect(ProtocolID, Flags, Baudrate, _channelID) != STATUS_NOERROR) {
			throw std::runtime_error("Can't open channel");
		}
	}

	J2534Channel::~J2534Channel()
	{
		_j2534.PassThruDisconnect(_channelID);
	}

	J2534_ERROR_CODE J2534Channel::readMsgs(std::vector<PASSTHRU_MSG> msgs, unsigned long Timeout)
	{
		return _j2534.PassThruReadMsgs(_channelID, msgs, Timeout);
	}

	J2534_ERROR_CODE J2534Channel::writeMsgs(const std::vector<PASSTHRU_MSG> msgs, unsigned long& numMsgs, unsigned long Timeout)
	{
		return _j2534.PassThruWriteMsgs(_channelID, msgs, numMsgs, Timeout);
	}

	J2534_ERROR_CODE J2534Channel::startPeriodicMsg(const PASSTHRU_MSG& msg, unsigned long& msgID, unsigned long TimeInterval)
	{
		return _j2534.PassThruStartPeriodicMsg(_channelID, msg, msgID, TimeInterval);
	}

	J2534_ERROR_CODE J2534Channel::stopPeriodicMsg(unsigned long MsgID)
	{
		return _j2534.PassThruStopPeriodicMsg(_channelID, MsgID);
	}

	J2534_ERROR_CODE J2534Channel::startMsgFilter(unsigned long FilterType, const PASSTHRU_MSG& maskMsg, const PASSTHRU_MSG& patternMsg,
		const PASSTHRU_MSG& flowControlMsg, unsigned long& msgID)
	{
		return _j2534.PassThruStartMsgFilter(_channelID, FilterType, maskMsg, patternMsg, flowControlMsg, msgID);
	}

	J2534_ERROR_CODE J2534Channel::stopMsgFilter(unsigned long MsgID)
	{
		return _j2534.PassThruStopMsgFilter(_channelID, MsgID);
	}

	J2534_ERROR_CODE J2534Channel::passThruIoctl(unsigned long IoctlID, const std::vector<uint8_t>& input, std::vector<uint8_t>& output)
	{
		return _j2534.PassThruIoctl(_channelID, IoctlID, input, output);
	}

} // namespace J2534
