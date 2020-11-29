#include "Logger.h"

#include "J2534Channel.hpp"
#include "CEMCanMessage.hpp"

#include <algorithm>

namespace logger
{
	enum ECUType {
		ECM = 0x7A,
		DEM = 0x1A,
		TCM = 0xE6
	};

	static CEMCanMessage makeCanMessage(ECUType ecuType, std::vector<uint8_t> request)
	{
		const uint8_t requestLength = 0xC8 + 1 + request.size();
		request.insert(request.begin(), ecuType);
		request.insert(request.begin(), requestLength);
		return CEMCanMessage(request);
	}

	static CEMCanMessage makeRegisterAddrRequest(uint32_t addr, uint8_t dataLength)
	{
		uint8_t byte1 = (addr & 0xFF0000) >> 16;
		uint8_t byte2 = (addr & 0xFF00) >> 8;
		uint8_t byte3 = (addr & 0xFF);
		return makeCanMessage(ECM, { 0xAA, 0x50, byte1, byte2, byte3, dataLength });
	}

	static const CEMCanMessage requestMemory{ makeCanMessage(ECM, { 0xA6, 0xF0, 0x00, 0x01 }) };
	static const CEMCanMessage unregisterAllMemoryRequest{ makeCanMessage(ECM, { 0xAA, 0x00 }) };

	static PASSTHRU_MSG makePassThruMsg(unsigned long ProtocolID, unsigned long Flags, const std::vector<unsigned char>& data)
	{
		PASSTHRU_MSG result;
		result.ProtocolID = ProtocolID;
		result.RxStatus = 0;
		result.TxFlags = Flags;
		result.Timestamp = 0;
		result.ExtraDataIndex = 0;
		result.DataSize = data.size();
		std::copy(data.begin(), data.end(), result.Data);
		return result;
	}

	static std::vector<PASSTHRU_MSG> makePassThruMsgs(unsigned long ProtocolID, unsigned long Flags, const std::vector<std::vector<unsigned char>>& data)
	{
		std::vector<PASSTHRU_MSG> result;
		for (const auto msgData: data) {
			PASSTHRU_MSG msg;
			msg.ProtocolID = ProtocolID;
			msg.RxStatus = 0;
			msg.TxFlags = Flags;
			msg.Timestamp = 0;
			msg.ExtraDataIndex = 0;
			msg.DataSize = msgData.size();
			std::copy(msgData.begin(), msgData.end(), msg.Data);
			result.emplace_back(std::move(msg));
		}
		return result;
	}

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

		unsigned long baudrate = 500000;

		_channel1 = openChannel(CAN_XON_XOFF, CAN_29BIT_ID, baudrate);
		_channel2 = openChannel(CAN_XON_XOFF, CAN_29BIT_ID | 0x20000000, 125000);
		if (baudrate != 500000)
			_channel3 = openBridgeChannel();
	}

	void Logger::stop()
	{
		{
			std::unique_lock<std::mutex> lock{ _mutex };
			_stopped = true;
		}
		if (_loggingThread.joinable())
			_loggingThread.join();

		_channel1.reset();
		_channel2.reset();
		_channel3.reset();
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
		config[2].Value = (Baudrate == 500000 ? 80 : 68);
		channel->setConfig(config);

		PASSTHRU_MSG msgFilter = makePassThruMsg(ProtocolID, Flags, { 0x00, 0x00, 0x00, 0x01 });
		unsigned long msgId;
		channel->startMsgFilter(PASS_FILTER, &msgFilter, &msgFilter, nullptr, msgId);
		startXonXoffMessageFiltering(*channel, Flags);
		startXonXoffMessageFiltering(*channel, 0);
		return std::move(channel);
	}

	std::unique_ptr<j2534::J2534Channel> Logger::openBridgeChannel()
	{
		const unsigned long ProtocolId = ISO9141;
		const unsigned long Flags = ISO9141_K_LINE_ONLY;
		auto channel{ std::make_unique<j2534::J2534Channel>(_j2534, ProtocolId, Flags, 10400) };
		std::vector<SCONFIG> config(4);
		config[0].Parameter = PARITY;
		config[0].Value = 0;
		config[1].Parameter = W0;
		config[1].Value = 60;
		config[2].Parameter = W1;
		config[2].Value = 600;
		config[3].Parameter = P4_MIN;
		config[3].Value = 0;
		channel->setConfig(config);

		PASSTHRU_MSG msg = makePassThruMsg(ProtocolId, Flags, { 0x84, 0x40, 0x13, 0xb2, 0xf0, 0x03 });
		unsigned long msgId;
		channel->startPeriodicMsg(msg, msgId, 2000);

		return std::move(channel);
	}

	void Logger::startXonXoffMessageFiltering(j2534::J2534Channel& channel, unsigned long Flags)
	{
		auto msgs{ makePassThruMsgs(CAN_XON_XOFF, Flags, {
			{ 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0x00 },
			{ 0x00, 0x00, 0x00, 0x01, 0x00, 0xA9, 0x00, 0x00 },
			{ 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0x00 },
			{ 0x00, 0x00, 0x00, 0x01, 0x00, 0xA9, 0x01, 0x00 },
			{ 0x00, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0x00 },
			{ 0x00, 0x00, 0x00, 0x01, 0x00, 0xA9, 0x02, 0x00 },
		}) };

		channel.passThruIoctl(CAN_XON_XOFF_FILTER, msgs.data());
		channel.passThruIoctl(CAN_XON_XOFF_FILTER_ACTIVE, nullptr);
	}

} // namespace logger
