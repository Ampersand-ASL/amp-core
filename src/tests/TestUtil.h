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
    uint32_t rxTime = 0;
    uint32_t remoteTime = 0;
    Type type = Type::UNKNOWN;
    int token = 0;
};

void play(Log& log, unsigned gapT, unsigned lastT, std::vector<TestInput>& ins,
    SequencingBuffer<int>& jb, SequencingBufferSink<int>* sink, 
    bool displayTick = true);
}
