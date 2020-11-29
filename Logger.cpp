#include "Logger.h"

#include "J2534Channel.hpp"

namespace logger
{
	Logger::Logger(j2534::J2534& j2534)
		: _j2534{j2534}
		, _loggingThread{}
		, _stopped{true}
	{
	}

	Logger::~Logger()
	{
		stop();
	}

	void Logger::start(const LogParameters& parameters, const std::wstring& savePath)
	{
		std::unique_lock<std::mutex> lock{ _mutex };
		if (!_stopped) {
			throw std::runtime_error("Logging already started");
		}

		_parameters = parameters;

		_channel1 = openChannel(CAN_XON_XOFF, CAN_29BIT_ID, 500000);
		_channel2 = openChannel(CAN_XON_XOFF, CAN_29BIT_ID | 0x20000000, 125000);
	}

	void Logger::stop()
	{
		{
			std::unique_lock<std::mutex> lock{ _mutex };
			_stopped = true;
		}
		if (_loggingThread.joinable())
			_loggingThread.join();
	}

	std::unique_ptr<j2534::J2534Channel> Logger::openChannel(unsigned long ProtocolID, unsigned long Flags, unsigned long Baudrate)
	{
		auto channel{ std::make_unique<j2534::J2534Channel>(_j2534, ProtocolID, Flags, Baudrate) };
		std::vector<SCONFIG> config(3);
		config[0].Parameter = DATA_RATE;
		config[0].Value = Baudrate;
		config[1].Parameter = LOOPBACK;
		config[1].Value = 0;
		config[2].Parameter = BIT_SAMPLE_POINT;
		config[2].Value = Baudrate == 500000 ? 80 : 68;
		channel->setConfig(config);

		PASSTHRU_MSG msgFilter;
		msgFilter.ProtocolID = ProtocolID;
		msgFilter.RxStatus = 0;
		msgFilter.TxFlags = Flags;
		msgFilter.Timestamp = 0;
		msgFilter.ExtraDataIndex = 0;
		msgFilter.DataSize = 4;
		msgFilter.Data[0] = 0x00;
		msgFilter.Data[1] = 0x00;
		msgFilter.Data[2] = 0x00;
		msgFilter.Data[3] = 0x01;

		unsigned long msgId;
		channel->startMsgFilter(PASS_FILTER, &msgFilter, &msgFilter, nullptr, msgId);
		return std::move(channel);
	}

} // namespace logger