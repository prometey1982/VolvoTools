#include "logger/Logger.hpp"

#include "logger/LoggerCallback.hpp"

#include <common/CommonData.hpp>
#include <common/D2Message.hpp>
#include <common/D2Messages.hpp>
#include <common/UDSMessage.hpp>
#include <common/UDSProtocolBase.hpp>
#include <common/UDSProtocolCommonSteps.hpp>
#include <common/Util.hpp>
#include <j2534/J2534.hpp>
#include <j2534/J2534Channel.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <numeric>

namespace logger {

	class LoggerImpl {
	public:
		LoggerImpl(j2534::J2534& j2534) : _j2534(j2534) {}

		virtual void registerParameters(j2534::J2534Channel& channel,
			const LogParameters& parameters) = 0;
		virtual std::vector<uint32_t>
			requestMemory(j2534::J2534Channel& channel,
				const LogParameters& parameters) = 0;

		virtual std::vector<std::unique_ptr<j2534::J2534Channel>> openChannels(unsigned long baudrate) = 0;

	protected:
		j2534::J2534& _j2534;
	};

	class D2LoggerImpl : public LoggerImpl {
	public:
		D2LoggerImpl(j2534::J2534& j2534) : LoggerImpl(j2534) {}

	private:
		const unsigned long protocolId = CAN;
		const unsigned long flags = CAN_29BIT_ID;

		const std::vector<PASSTHRU_MSG> requstMemoryMessage{
			common::D2Messages::requestMemory.toPassThruMsgs(protocolId, flags) };

		unsigned long getNumberOfCanMessages(const LogParameters& parameters) const {
			double totalDataLength =
				std::accumulate(parameters.parameters().cbegin(), parameters.parameters().cend(), static_cast<size_t>(0),
					[](size_t prevValue, const auto& param) {
						return prevValue + param.size();
					});
			return static_cast<unsigned long>(std::ceil((totalDataLength - 3) / 7)) + 1;
		}

		virtual void registerParameters(j2534::J2534Channel& channel,
			const LogParameters& parameters) override {
			unsigned long numMsgs;
			channel.writeMsgs(
				common::D2Messages::unregisterAllMemoryRequest.toPassThruMsgs(
					protocolId, flags),
				numMsgs);
			std::vector<PASSTHRU_MSG> result(1);
			channel.readMsgs(result);
			for (const auto parameter : parameters.parameters()) {
				const auto registerParameterRequest{
					common::D2Messages::makeRegisterAddrRequest(parameter.addr(),
																parameter.size()) };
				unsigned long numMsgs;
				channel.writeMsgs(
					registerParameterRequest.toPassThruMsgs(protocolId, flags), numMsgs);
				if (numMsgs == 0) {
					throw std::runtime_error("Request to the ECU wasn't send");
				}
				result.resize(1);
				channel.readMsgs(result);
				if (result.empty()) {
					throw std::runtime_error("ECU didn't response intime");
				}
				for (const auto& msg : result) {
					if (msg.Data[6] != 0xEA)
						throw std::runtime_error("Can't register memory addr");
				}
			}
		}

		virtual std::vector<uint32_t>
			requestMemory(j2534::J2534Channel& channel,
				const LogParameters& parameters) override {
			const auto numberOfCanMessages = getNumberOfCanMessages(parameters);
			std::vector<uint32_t> result;
			unsigned long writtenCount = 1;
			channel.writeMsgs(requstMemoryMessage, writtenCount);
			if (writtenCount > 0) {
				std::vector<PASSTHRU_MSG> logMessages(numberOfCanMessages);
				channel.readMsgs(logMessages);

				result.reserve(parameters.parameters().size());

				size_t paramIndex = 0;
				size_t paramOffset = 0;
				uint16_t value = 0;
				for (const auto& msg : logMessages) {
					size_t msgOffset = 5;
					// E6 F0 00 - read record by identifier answer
					if (msg.Data[4] == 0x8F &&
						msg.Data[5] == static_cast<uint8_t>(common::ECUType::ECM_ME) &&
						msg.Data[6] == 0xE6 && msg.Data[7] == 0xF0 && msg.Data[8] == 0)
						msgOffset = 9;
					for (size_t i = msgOffset; i < 12; ++i) {
						const auto& param = parameters.parameters()[paramIndex];
						value += msg.Data[i] << ((param.size() - paramOffset - 1) * 8);
						++paramOffset;
						if (paramOffset >= param.size()) {
							result.push_back(value);
							++paramIndex;
							paramOffset = 0;
							value = 0;
						}
						if (paramIndex >= parameters.parameters().size())
							break;
					}
				}
			}
			return result;
		}

		virtual std::vector<std::unique_ptr<j2534::J2534Channel>> openChannels(unsigned long baudrate) override {
			std::vector<std::unique_ptr<j2534::J2534Channel>> result;

			const unsigned long protocolId = CAN;
			const unsigned long flags = CAN_29BIT_ID;
			result.emplace_back(common::openChannel(_j2534, protocolId, flags, baudrate));

			if (baudrate != 500000) {
				result.emplace_back(common::openBridgeChannel(_j2534));
			}
			return result;
		}

	};

	class UDSLoggerImpl : public LoggerImpl, private common::UDSProtocolBase {
	public:
		UDSLoggerImpl(j2534::J2534& j2534, uint32_t canId)
			: LoggerImpl(j2534)
			, UDSProtocolBase(j2534, canId)
			, _didBase(0xF200)
		{
		}

	private:
		virtual void
			registerParameters(j2534::J2534Channel& channel,
				const LogParameters& parameters) override {

			common::UDSMessage diagSessionRequest(getCanId(), { 0x10, 0x03 });
			unsigned long numMsgs;
			if (channel.writeMsgs(diagSessionRequest, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
				return;
			}
			std::vector<PASSTHRU_MSG> read_msgs;
			read_msgs.resize(1);
			if (channel.readMsgs(read_msgs, 1000) != STATUS_NOERROR || read_msgs.empty())
			{
				return;
			}
			uint16_t did = _didBase;
			for (const auto& param : parameters.parameters()) {
				common::UDSMessage clearDDDIRequest(getCanId(), { 0x2C, 0x03, static_cast<uint8_t>(did >> 8), static_cast<uint8_t>(did) });
				if (channel.writeMsgs(clearDDDIRequest, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
					return;
				}
				read_msgs.resize(1);
				if (channel.readMsgs(read_msgs, 1000) != STATUS_NOERROR || read_msgs.empty())
				{
					return;
				}
				constexpr uint8_t addrLength = 4;
				constexpr uint8_t dataLength = 2;
				constexpr uint8_t dataFormat = (dataLength << 4) + addrLength;
				std::vector<uint8_t> formattedParams{ 0x2C, 0x02, static_cast<uint8_t>(did >> 8), static_cast<uint8_t>(did), dataFormat };
				const auto formattedAddr = common::toVector(param.addr());
				formattedParams.insert(formattedParams.end(), formattedAddr.cbegin(), formattedAddr.cend());
				formattedParams.push_back(static_cast<uint8_t>(param.size() >> 8));
				formattedParams.push_back(static_cast<uint8_t>(param.size()));
				common::UDSMessage registerMessage(getCanId(), formattedParams);
				if (channel.writeMsgs(registerMessage, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
					return;
				}
				read_msgs.resize(1);
				if (channel.readMsgs(read_msgs, 1000) != STATUS_NOERROR || read_msgs.empty())
				{
					return;
				}
				_didList.push_back(did++);
			}
		}
		virtual std::vector<uint32_t>
			requestMemory(j2534::J2534Channel& channel,
				const LogParameters& parameters) override {
			std::vector<uint32_t> result;
			size_t paramIndex = 0;
			size_t paramOffset = 0;
			uint32_t value = 0;
			for (const auto& did : _didList) {
				common::UDSMessage requestDidRequest(getCanId(), { 0x22, static_cast<uint8_t>(did >> 8), static_cast<uint8_t>(did) });
				unsigned long numMsgs;
				if (channel.writeMsgs(requestDidRequest, numMsgs) != STATUS_NOERROR || numMsgs < 1) {
					return result;
				}
				std::vector<PASSTHRU_MSG> read_msgs;
				read_msgs.resize(1);//parameters.parameters()[paramIndex].size() > 2 ? 2 : 1);
				if (channel.readMsgs(read_msgs, 1000) != STATUS_NOERROR || read_msgs.empty())
				{
					return result;
				}
				for (const auto& msg : read_msgs) {
					for (size_t i = 7; i < msg.DataSize; ++i) {
						const auto& param = parameters.parameters()[paramIndex];
						value += msg.Data[i] << ((param.size() - paramOffset - 1) * 8);
						++paramOffset;
						if (paramOffset >= param.size()) {
							result.push_back(value);
							++paramIndex;
							paramOffset = 0;
							value = 0;
						}
						if (paramIndex >= parameters.parameters().size()) {
							return result;
						}
					}
				}
			}
			return result;
		}

		virtual std::vector<std::unique_ptr<j2534::J2534Channel>> openChannels(unsigned long baudrate) override {
			std::vector<std::unique_ptr<j2534::J2534Channel>> result;

			result.emplace_back(common::openUDSChannel(getJ2534(), baudrate, getCanId()));

			return result;
		}

		std::vector<std::unique_ptr<j2534::J2534Channel>> _channels;
		const uint16_t _didBase;
		std::vector<uint16_t> _didList;
	};

	std::unique_ptr<LoggerImpl> createLoggerImpl(const std::vector<common::ConfigurationInfo>& configurationInfo,
		common::CarPlatform carPlatform, uint32_t cmId, const std::string& cmInfo, j2534::J2534& j2534)
	{
		using common::CarPlatform;
		if (cmId == 0x7A && (carPlatform == CarPlatform::P80 || carPlatform == CarPlatform::P1
			|| carPlatform == CarPlatform::P2 || carPlatform == CarPlatform::P2_250)) {
			return std::make_unique<D2LoggerImpl>(j2534);
		}
		if (cmId == 0x6A && (carPlatform == CarPlatform::P80 || carPlatform == CarPlatform::P2
			|| carPlatform == CarPlatform::P2_250)) {
			if (common::toLower(cmInfo) == "aw55") {
				throw std::runtime_error("Need to implement logger for aw55");
				return {};
			}
			else if (common::toLower(cmInfo) == "tf80") {
				throw std::runtime_error("Need to implement logger for tf80");
				return {};
			}
		}
		if (carPlatform == CarPlatform::P3 || carPlatform == CarPlatform::Ford) {
			const common::ECUInfo ecuInfo = std::get<1>(common::getEcuInfoByEcuId(configurationInfo, carPlatform, cmId));
			return std::make_unique<UDSLoggerImpl>(j2534, ecuInfo.canId);
		}
		throw std::runtime_error("Not implemented");
	}

	LoggerType getLoggerType(common::CarPlatform carPlatform)
	{
		switch (carPlatform)
		{
		case common::CarPlatform::P1:
		case common::CarPlatform::P2:
		case common::CarPlatform::P80:
			return LoggerType::LT_D2;
		case common::CarPlatform::P3:
		case common::CarPlatform::Ford:
			return LoggerType::LT_UDS;
		default:
			throw std::runtime_error("Unsupported car platform");
		}
	}

	Logger::Logger(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t cmId, const std::string& cmInfo)
		: _configurationInfo{ common::loadConfiguration(common::CommonData::commonConfiguration) }
		, _j2534{ j2534 }
		, _carPlatform{ carPlatform }
		, _cmId{ cmId }
		, _cmInfo{ cmInfo }
		, _loggingThread{}
		, _stopped{ true }
		, _loggerImpl(createLoggerImpl(_configurationInfo, _carPlatform, _cmId, _cmInfo, j2534)) {
	}

	Logger::~Logger() { stop(); }

	void Logger::registerCallback(LoggerCallback& callback) {
		std::unique_lock<std::mutex> lock{ _callbackMutex };
		if (std::find(_callbacks.cbegin(), _callbacks.cend(), &callback) ==
			_callbacks.cend()) {
			_callbacks.push_back(&callback);
		}
	}

	void Logger::unregisterCallback(LoggerCallback& callback) {
		std::unique_lock<std::mutex> lock{ _callbackMutex };
		_callbacks.erase(std::remove(_callbacks.begin(), _callbacks.end(), &callback),
			_callbacks.end());
	}

	void Logger::start(unsigned long baudrate, const LogParameters& parameters) {
		std::unique_lock<std::mutex> lock{ _mutex };
		if (!_stopped) {
			throw std::runtime_error("Logging already started");
		}

		_parameters = parameters;

		_channels = _loggerImpl->openChannels(baudrate);

		registerParameters();

		_stopped = false;

		_callbackThread = std::thread([this]() { callbackFunction(); });

		_loggingThread = std::thread([this]() { logFunction(); });
	}

	void Logger::stop() {
		{
			std::unique_lock<std::mutex> lock{ _mutex };
			_stopped = true;
		}
		if (_loggingThread.joinable())
			_loggingThread.join();

		{
			std::unique_lock<std::mutex> lock{ _callbackMutex };
			_callbackCond.notify_all();
		}

		if (_callbackThread.joinable())
			_callbackThread.join();

		_channels.clear();
	}

	void Logger::registerParameters() {
		auto& channel = *_channels[0]; // Always first channel for the logger.
		_loggerImpl->registerParameters(channel, _parameters);
	}

	void Logger::logFunction() {
		{
			std::unique_lock<std::mutex> lock{ _callbackMutex };
			for (const auto callback : _callbacks) {
				callback->onStatusChanged(true);
			}
		}
		auto& channel = *_channels[0]; // Always first channel for the logger.
		const auto startTimepoint{ std::chrono::steady_clock::now() };
		for (size_t timeoffset = 0;; timeoffset += 50) {
			{
				std::unique_lock<std::mutex> lock{ _mutex };
				if (_stopped)
					break;
			}
			channel.clearRx();
			channel.clearTx();
			auto logRecord = _loggerImpl->requestMemory(channel, _parameters);
			const auto now{ std::chrono::steady_clock::now() };
			pushRecord(LogRecord(std::chrono::duration_cast<std::chrono::milliseconds>(
				now - startTimepoint),
				std::move(logRecord)));
			std::unique_lock<std::mutex> lock{ _mutex };
			_cond.wait_until(lock,
				startTimepoint + std::chrono::milliseconds(timeoffset));
		}
		{
			std::unique_lock<std::mutex> lock{ _callbackMutex };
			for (const auto callback : _callbacks) {
				callback->onStatusChanged(false);
			}
		}
	}

	void Logger::pushRecord(Logger::LogRecord&& record) {
		std::unique_lock<std::mutex> lock{ _callbackMutex };
		_loggedRecords.emplace_back(std::move(record));
		_callbackCond.notify_all();
	}

	void Logger::callbackFunction() {
		for (;;) {
			LogRecord logRecord;
			{
				std::unique_lock<std::mutex> lock{ _callbackMutex };
				_callbackCond.wait(
					lock, [this] { return _stopped || !_loggedRecords.empty(); });
				if (_stopped)
					break;
				logRecord = _loggedRecords.front();
				_loggedRecords.pop_front();
			}
			std::vector<double> formattedValues(logRecord.values.size());
			for (size_t i = 0; i < logRecord.values.size(); ++i) {
				formattedValues[i] =
					_parameters.parameters()[i].formatValue(logRecord.values[i]);
			}
			{
				std::unique_lock<std::mutex> lock{ _callbackMutex };
				for (const auto callback : _callbacks) {
					callback->onLogMessage(logRecord.timePoint, formattedValues);
				}
			}
		}
	}

} // namespace logger
