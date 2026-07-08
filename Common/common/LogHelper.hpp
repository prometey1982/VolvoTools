#pragma once

#ifndef LOG_MODULE_NAME
#error "LOG_MODULE_NAME must be set"
#endif

#include <chrono>
#include <easylogging++.h>

#undef LOG_MODULE
#define LOG_MODULE(level) CLOG(level, LOG_MODULE_NAME)

class ScopedTimer {
public:
    ScopedTimer(const char* name, const char* module)
        : _name(name), _module(module), _start(std::chrono::steady_clock::now()) {}
    ~ScopedTimer() {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - _start).count();
        CLOG(INFO, _module) << _name << " took " << ms << " ms";
    }
private:
    const char* _name;
    const char* _module;
    std::chrono::time_point<std::chrono::steady_clock> _start;
};

#define LOG_SCOPE_DURATION(name) ScopedTimer _scopedTimer_##name(#name, LOG_MODULE_NAME)
