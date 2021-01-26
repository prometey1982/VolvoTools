#include "FileLogWriter.hpp"

#include "LogParameters.hpp"

namespace logger {

  FileLogWriter::FileLogWriter(const std::string &outputPath,
                const logger::LogParameters &parameters) {
      open(outputPath, parameters);
  }

  void FileLogWriter::open(const std::string &outputPath,
            const logger::LogParameters &parameters)
  {
     _outputStream.open(outputPath);
      _outputStream << "Time (sec),";
      const auto startTimepoint{std::chrono::steady_clock::now()};
      unsigned long numberOfCanMessages{0};
      numberOfCanMessages = parameters.getNumberOfCanMessages();
      for (const auto &param : parameters.parameters()) {
        _outputStream << param.name() << "(" << param.unit() << "),";
      }
      _outputStream << std::endl;
  }

  void FileLogWriter::onLogMessage(std::chrono::milliseconds timePoint,
                    const std::vector<double> &values) {
    _outputStream << (timePoint.count() / 1000.0) << ",";

    for (const auto value : values) {
      _outputStream << value << ",";
    }
    _outputStream << std::endl;
  }

}
