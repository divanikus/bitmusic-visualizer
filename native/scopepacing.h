#pragma once
#include <chrono>

namespace ScopePacing {
constexpr int DefaultFps = 30;
constexpr int normalize(int fps) { return fps == 60 || fps == 120 ? fps : DefaultFps; }
constexpr auto interval(int fps) { return std::chrono::nanoseconds(1'000'000'000 / normalize(fps)); }
}
