#include <array>
#include <cstdint>
#include <vector>

#include "../j2534/J2534_v0404.h"

namespace common {

class CanMessage {
public:
  static constexpr size_t CanPayloadSize = 8;
  using DataType = std::array<uint8_t, CanPayloadSize>;

  virtual std::vector<PASSTHRU_MSG> toPassThruMsgs(unsigned long ProtocolID,
                                           unsigned long Flags) const = 0;

protected:
  explicit CanMessage(const std::vector<DataType> &data);

  const std::vector<DataType>& data() const;

private:
  const std::vector<DataType> _data;
};

}
