#include <iostream>
#include <cassert>
#include <vector>
#include <queue>

#include "kc1fsz-tools/Log.h"
#include "amp/SequencingBufferStd.h"

#include "IAX2Util.h"
#include "Message.h"
#include "tests/TestUtil.h"

using namespace std;
using namespace kc1fsz;
using namespace kc1fsz::amp;

class TestSink2 : public SequencingBufferSink<int> {
public:

    unsigned signalCount = 0;
    unsigned voiceCount = 0;
    unsigned interpolateCount = 0;

    TestSink2(vector<TestInput>& outs)
    :   _outs(outs) {
    }

    virtual void playSignal(const int& payload, uint32_t localMs) {
        _outs.push_back({ .rxMs=localMs, .origMs=0, .type=Type::SIGNAL, .token=payload });
        cout << "Release signal: t=" << localMs << " payload=" << payload << endl;
        signalCount++;
    };

    virtual void playVoice(const int& payload, uint32_t localMs) {
        _outs.push_back({ .rxMs=localMs, .origMs=0, .type=Type::VOICE, .token=payload });
        cout << "Release voice: t=" << localTime << " payload=" << (long)payload << endl;
        voiceCount++;
    };

    virtual void interpolateVoice(uint32_t localMs, uint32_t durationMs) {
        _outs.push_back({ .rxMs=localMs, .origMs=0, .type=Type::VOICE_INTERPOLATE });
        cout << "Interpolate voice: t=" << localMs << endl;
        interpolateCount++;
    };

private:

    vector<TestInput>& _outs;
};

struct Payload {
    uint32_t origMs;
    uint32_t rxMs;
    bool voice;
    unsigned id;
    uint32_t getOrigMs() const { return origMs; }
    uint32_t getRxMs() const { return rxMs; }
    bool isVoice() const { return voice; }
};

void test_1() {

    cout << endl;
    cout << "===== Test 1 ==========================================" << endl;
    cout << endl;

    Log log;
    std::vector<TestInput> outs;
    TestSink2 sink(outs);
    SequencingBufferStd<Payload> jb;
    jb.setInitialMargin(20);
    jb.lockDelay();
 
    bool b;

    b = jb.consume(log, { .origMs = 20, .rxMs = 120, .voice=true, .id=1 });
    assert(b);
}
