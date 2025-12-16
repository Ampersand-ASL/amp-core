#pragma once

#include <cstdint>
#include <vector>

#include "amp/SequencingBuffer.h"
#include "amp/SequencingBufferStd.h"

using namespace kc1fsz::amp;

namespace kc1fsz {

class Log;

class TestClock : public Clock {
public:

    TestClock(Log& log) : _log(log) { }

    uint32_t time() const { return _timeMs; }

    void setTime(uint32_t ms) { 
        _timeMs = ms;
        _log.info("----- Time %u -----", _timeMs); 
    }

    void increment(uint32_t ms) {
        setTime(time() + ms);
    }

    Log& _log;
    uint32_t _timeMs = 0;
};

struct TestFrame {
    uint32_t origMs;
    uint32_t rxMs;
    bool voice = true;
    unsigned id = 0;
    uint32_t getOrigMs() const { return origMs; }
    uint32_t getRxMs() const { return rxMs; }
    bool isVoice() const { return voice; }
};

void play(Log& log, unsigned gapMs, unsigned lastMs, std::vector<TestFrame>& ins,
    SequencingBuffer<TestFrame>& jb, SequencingBufferSink<TestFrame>* sink, 
    bool displayTick = true, 
    std::function<void(unsigned tickMs)> checkCb = nullptr);
}
