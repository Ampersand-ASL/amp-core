#pragma once

#include <cstdint>
#include <vector>

#include "amp/SequencingBuffer.h"
#include "amp/SequencingBufferStd.h"

using namespace kc1fsz::amp;

namespace kc1fsz {

class Log;

enum Type { UNKNOWN, SIGNAL, VOICE, VOICE_INTERPOLATE };

struct TestInput {
    uint32_t rxMs = 0;
    uint32_t origMs = 0;
    Type type = Type::UNKNOWN;
    int token = 0;
};

void play(Log& log, unsigned gapMs, unsigned lastMs, std::vector<TestInput>& ins,
    SequencingBuffer<int>& jb, SequencingBufferSink<int>* sink, 
    bool displayTick = true);
}
