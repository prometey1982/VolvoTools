#pragma once

#include "LoggerCallback.hpp"

#include <fstream>

namespace logger {
class LogParameters;

class FileLogWriter final : public logger::LoggerCallback {
public:
  FileLogWriter() = default;
  FileLogWriter(const std::string &outputPath,
                const logger::LogParameters &parameters);

  void open(const std::string &outputPath,
            const logger::LogParameters &parameters);

  void onLogMessage(std::chrono::milliseconds timePoint,
                    const std::vector<double> &values) override;
  void onStatusChanged(bool /*started*/) override {}

private:
  std::ofstream _outputStream;
};

}
