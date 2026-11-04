#pragma once

#include <cstddef>

namespace streambrew {

enum class DeliveryTiming {

    // Deliver as soon as decoding and the consumer allow
    AsFastAsPossible,

    // Wait for video timestamps on the pipeline clock
    Clocked

};

enum class OverflowPolicy {

    // Wait for queue space and preserve queued frames in order
    BlockProducer,

    // Discard the oldest pending frame when the queue is full.
    // Only preserve newer frames
    DropOldest

};

struct PipelineOptions {

    DeliveryTiming timing = DeliveryTiming::Clocked;
    OverflowPolicy overflow = OverflowPolicy::BlockProducer;

    // Appsink queue capacity
    // Must be in [1, G_MAXUINT].
    std::size_t max_pending_frames = 1;
};

} // namespace streambrew
