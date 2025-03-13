#pragma once

#include "FlasherCallback.hpp"

#include "FlasherBase.hpp"

#include <common/GenericProcess.hpp>
#include <common/CMType.hpp>
#include <common/D2Messages.hpp>
#include <common/VBF.hpp>

namespace flasher {

class D2FlasherBase: public FlasherBase {
public:
    explicit D2FlasherBase(j2534::J2534 &j2534, FlasherParameters&& flasherParameters);
    ~D2FlasherBase();

    void canWakeUp(unsigned long baudrate);

protected:
    void startImpl(std::vector<std::unique_ptr<j2534::J2534Channel>>& channels) override final;

    virtual size_t getMaximumFlashProgress() const = 0;
    virtual void eraseStep(j2534::J2534Channel &channel, uint8_t ecuId) = 0;
    virtual void writeStep(j2534::J2534Channel &channel, uint8_t ecuId) = 0;

  void canWakeUp();
  void cleanErrors();
};

} // namespace flasher
