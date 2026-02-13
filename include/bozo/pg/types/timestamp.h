#pragma once

#include <bozo/ext/std/time_point.h>

namespace bozo::pg {
using timestamp = std::chrono::system_clock::time_point;
} // namespace bozo:pg
