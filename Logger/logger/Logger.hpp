#pragma once

#include "LogParameters.hpp"

#include <common/CarPlatform.hpp>
#include <common/ConfigurationInfo.hpp>
#include <common/J2534ChannelProvider.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace logger {
	class LoggerCallback;

	enum class LoggerType { LT_D2, LT_UDS };

	class LoggerImpl;

	class Logger final {
	public:
        explicit Logger(j2534::J2534& j2534, common::CarPlatform carPlatform, uint32_t ecuId, const std::string& cmInfo);
		~Logger();

		void registerCallback(LoggerCallback& callback);
		void unregisterCallback(LoggerCallback& callback);

		void start(unsigned long baudrate, const LogParameters& parameters);
		void stop();

	private:
		void registerParameters();

		void logFunction();

		struct LogRecord {
			LogRecord() = default;
			LogRecord(std::chrono::milliseconds timePoint,
				std::vector<uint32_t>&& values)
				: timePoint{ timePoint }, values(std::move(values)) {
			}
			std::chrono::milliseconds timePoint;
			std::vector<uint32_t> values;
		};

		void pushRecord(LogRecord&& record);
		void callbackFunction();

	private:
        common::J2534ChannelProvider _j2534ChannelProvider;
		common::CarPlatform _carPlatform;
        uint32_t _ecuId;
		std::string _cmInfo;
		LogParameters _parameters;
		std::thread _loggingThread;
		std::thread _callbackThread;
		std::mutex _mutex;
		std::mutex _callbackMutex;
		std::condition_variable _cond;
		std::condition_variable _callbackCond;
		bool _stopped;

		std::unique_ptr<LoggerImpl> _loggerImpl;

		std::deque<LogRecord> _loggedRecords;
		std::vector<LoggerCallback*> _callbacks;
	};

} // namespace logger
