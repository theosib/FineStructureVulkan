#pragma once

/**
 * @file scroll_accumulator.hpp
 * @brief Utility for converting fractional scroll deltas into discrete ticks
 *
 * macOS trackpads generate many small fractional scroll events (0.1, 0.3, etc.)
 * while traditional mouse wheels produce clean +1.0/-1.0 per notch. This utility
 * accumulates fractional values and emits whole ticks when the threshold is reached.
 *
 * Usage:
 * @code
 * ScrollAccumulator acc;
 * acc.accumulate(0.3f);  // Not enough for a tick yet
 * acc.accumulate(0.8f);  // Total = 1.1
 * int ticks = acc.consumeTicks();  // Returns 1, remainder = 0.1
 * @endcode
 */

namespace finevk {

/**
 * @brief Accumulates fractional scroll deltas and emits discrete ticks
 *
 * Useful for interfaces that need discrete steps (hotbar cycling, list scrolling)
 * while preserving smooth behavior for trackpad users.
 */
class ScrollAccumulator {
public:
    /**
     * @brief Add a fractional scroll delta to the accumulator
     * @param delta Scroll delta (positive or negative)
     */
    void accumulate(float delta);

    /**
     * @brief Consume accumulated ticks and return the count
     *
     * Returns the number of whole ticks that have accumulated (positive or negative)
     * and resets the accumulator to the fractional remainder. For example, if the
     * accumulator holds 2.7, this returns 2 and leaves 0.7 in the accumulator.
     *
     * @return Number of whole ticks (can be negative for reverse scrolling)
     */
    int consumeTicks();

    /**
     * @brief Reset accumulator to zero
     */
    void reset();

    /**
     * @brief Get current fractional remainder without consuming
     * @return Fractional scroll value currently accumulated
     */
    [[nodiscard]] float remainder() const { return remainder_; }

private:
    float remainder_ = 0.0f;
};

} // namespace finevk
