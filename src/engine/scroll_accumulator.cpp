#include "finevk/engine/scroll_accumulator.hpp"
#include <cmath>

namespace finevk {

void ScrollAccumulator::accumulate(float delta) {
    remainder_ += delta;
}

int ScrollAccumulator::consumeTicks() {
    // Extract whole ticks (floor for negative, trunc for positive gives correct sign)
    int ticks = static_cast<int>(std::floor(remainder_));
    remainder_ -= static_cast<float>(ticks);
    return ticks;
}

void ScrollAccumulator::reset() {
    remainder_ = 0.0f;
}

} // namespace finevk
