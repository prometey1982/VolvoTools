#pragma once

namespace j2534 {
class J2534;
} // namespace j2534

namespace common {

class CanAlarmClock {
public:
    explicit CanAlarmClock(j2534::J2534& j2534);
    void start();

private:
    j2534::J2534& _j2534;
};

} // namespace common
