#pragma once

#include <chrono>

// Wall-clock seconds since program start (steady_clock). The first call to
// elapsed() from main() pins the start; Timer::start() makes that explicit.
namespace Timer {

inline std::chrono::steady_clock::time_point& origin() {
    static std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    return t0;
}

inline void start() { origin(); }

inline double elapsed() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - origin()).count();
}

}  // namespace Timer
