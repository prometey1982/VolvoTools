#pragma once

#include "LogParameter.hpp"

#include <string>
#include <vector>

namespace logger {

class LogParameters final {
public:
  LogParameters() = default;
  explicit LogParameters(const std::string &path);
  explicit LogParameters(std::istream &stream);
  LogParameters(const LogParameters &rhs) = default;

  const LogParameters &operator=(const LogParameters &rhs);

  const std::vector<LogParameter> &parameters() const;
  unsigned long getNumberOfCanMessages() const;

private:
  template<typename Reader>
  void load(Reader& reader);

private:
  std::vector<LogParameter> _parameters;
};

} // namespace logger
