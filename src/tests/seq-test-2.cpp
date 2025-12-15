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

class TestSink2 : public SequencingBufferSink<TestFrame> {
public:

    unsigned signalCount = 0;
    unsigned voiceCount = 0;
    unsigned interpolateCount = 0;

    TestSink2(vector<TestFrame>& outs)
    :   _outs(outs) {
    }

    virtual void playSignal(const TestFrame& payload, uint32_t localMs) {
        _outs.push_back(payload);
        cout << "Release signal: t=" << localMs << " origMs=" << payload.getOrigMs() << endl;
        signalCount++;
    };

    virtual void playVoice(const TestFrame& payload, uint32_t localMs) {
        _outs.push_back(payload);
        cout << "Release voice: t=" << localMs << " origMs=" << payload.getOrigMs() << endl;
        voiceCount++;
    };

    virtual void interpolateVoice(uint32_t origMs, uint32_t localMs, uint32_t durationMs) {
        _outs.push_back({ .origMs=0, .rxMs=localMs, .voice=true });
        cout << "Interpolate voice: t=" << localMs << " origMs=" << origMs << endl;
        interpolateCount++;
    };

private:

    vector<TestFrame>& _outs;
};

class TestSink3 : public SequencingBufferSink<Message> {
public:

    unsigned signalCount = 0;
    unsigned voiceCount = 0;
    unsigned interpolateCount = 0;

    virtual void playSignal(const Message& payload, uint32_t localMs) {
        cout << "Release signal: t=" << localMs << " origMs=" << payload.getOrigMs() << endl;
        signalCount++;
    };

    virtual void playVoice(const Message& payload, uint32_t localMs) {
        cout << "Release voice: t=" << localMs << " origMs=" << payload.getOrigMs() << endl;
        voiceCount++;
    };

    virtual void interpolateVoice(uint32_t origMs, uint32_t localMs, uint32_t durationMs) {
        cout << "Interpolate voice: t=" << localMs << " origMs=" << origMs << endl;
        interpolateCount++;
    };
};

void test_1() {

    Log log;

    // A very straight-forward example, demonstrates that the initial margin
    // was used properly.
    {
        cout << endl;
        cout << "===== Test 0a ==========================================" << endl;
        cout << endl;

        std::vector<TestFrame> outs;
        TestSink2 sink(outs);
        SequencingBufferStd<TestFrame> jb;
        jb.setInitialMargin(20);
        jb.lockDelay();
        std::vector<TestFrame> ins;
        ins.push_back({ .origMs = 20, .rxMs = 120, .voice=true, .id=1 });
        ins.push_back({ .origMs = 40, .rxMs = 140, .voice=true, .id=2 });
        ins.push_back({ .origMs = 60, .rxMs = 160, .voice=true, .id=3 });

        play(log, 20, 200, ins,jb, &sink, false, 
            [&](uint32_t t) {
                // Since the initial margin is 20 and the first packet arrives at 120,
                // we're not going to see any output until 140
                if (t < 140) {
                    assert(outs.empty());
                }
                else if (t == 140) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 20);
                    outs.clear();
                }
                else if (t == 160) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 40);
                    outs.clear();
                }
                else if (t == 180) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 60);
                    outs.clear();
                }
            }
        );
        assert(sink.voiceCount == 3);
        assert(sink.interpolateCount == 0);
    }

    // Just like the previous test, but showing that the remote timer could be ahead 
    // of the local one (doesn't matter)
    {
        cout << endl;
        cout << "===== Test 0b ==========================================" << endl;
        cout << endl;

        std::vector<TestFrame> outs;
        TestSink2 sink(outs);
        SequencingBufferStd<TestFrame> jb;
        jb.setInitialMargin(20);
        jb.lockDelay();
        std::vector<TestFrame> ins;
        ins.push_back({ .origMs = 920, .rxMs = 120, .voice=true, .id=1 });
        ins.push_back({ .origMs = 940, .rxMs = 140, .voice=true, .id=2 });
        ins.push_back({ .origMs = 960, .rxMs = 160, .voice=true, .id=3 });

        play(log, 20, 200, ins,jb, &sink, false, 
            [&](uint32_t t) {
                // Since the initial margin is 20 and the first packet arrives at 120,
                // we're not going to see any output until 140
                if (t < 140) {
                    assert(outs.empty());
                }
                else if (t == 140) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 920);
                    outs.clear();
                }
                else if (t == 160) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 940);
                    outs.clear();
                }
                else if (t == 180) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 960);
                    outs.clear();
                }
            }
        );
        assert(sink.voiceCount == 3);
        assert(sink.interpolateCount == 0);
    }

    // PATHOLOGY #1 - A late frame
    //
    // The frame arrives late, but still inside of the margin so it slots into
    // the sequence in the right place.
    {
        cout << endl;
        cout << "===== Test 1 ==========================================" << endl;
        cout << endl;

        std::vector<TestFrame> outs;
        TestSink2 sink(outs);
        SequencingBufferStd<TestFrame> jb;
        jb.setInitialMargin(20);
        jb.lockDelay();
        std::vector<TestFrame> ins;
        ins.push_back({ .origMs = 20, .rxMs = 120, .voice=true });
        // Notice arrival time is 10ms late
        ins.push_back({ .origMs = 40, .rxMs = 150, .voice=true });
        ins.push_back({ .origMs = 60, .rxMs = 160, .voice=true });

        play(log, 20, 200, ins, jb, &sink, false, 
            [&](uint32_t t) {
                // Since the initial margin is 20 and the first packet arrives at 120,
                // we're not going to see any output until 140
                if (t < 140) {
                    assert(outs.empty());
                }
                else if (t == 140) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 20);
                    outs.clear();
                }
                // NOTICE: Plays at the right time even though it arrived late.
                else if (t == 160) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 40);
                    outs.clear();
                }
                else if (t == 180) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 60);
                    outs.clear();
                }
            }
        );
    }

    // PATHOLOGY #2 - A lost frame.
    //
    // The second frame disappears completely and will need to be interpolated.
    {
        cout << endl;
        cout << "===== Test 2 ==========================================" << endl;
        cout << endl;

        std::vector<TestFrame> outs;
        TestSink2 sink(outs);
        SequencingBufferStd<TestFrame> jb;
        jb.setInitialMargin(20);
        jb.lockDelay();
        std::vector<TestFrame> ins;
        ins.push_back({ .origMs = 20, .rxMs = 120, .voice=true });
        // GAP: NO origMs = 40 (LOST)
        ins.push_back({ .origMs = 60, .rxMs = 160, .voice=true });

        play(log, 20, 300, ins, jb, &sink, false, 
            [&](uint32_t t) {
                // Since the initial margin is 20 and the first packet arrives at 120,
                // we're not going to see any output until 140
                if (t < 140) {
                    assert(outs.empty());
                }
                else if (t == 140) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 20);
                    outs.clear();
                }
                // NOTICE: Interpolated
                else if (t == 160) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 0);
                    outs.clear();
                }
                else if (t == 180) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 60);
                    outs.clear();
                }
                // There wil be 4 interpolated frames at the end
                else if (t >= 200 && t <= 240) {
                    assert(jb.inTalkspurt());
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 0);
                    outs.clear();
                }
                // Final interpolation
                else if (t == 260) {
                    assert(!jb.inTalkspurt());
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 0);
                    outs.clear();
                }
                // After the 4 interpolations we are no longer in the TS
                else if (t >= 260) {
                    assert(!jb.inTalkspurt());
                    assert(outs.size() == 0);
                }
            }
        );
        assert(sink.voiceCount == 2);
        assert(sink.interpolateCount == 1 + 4);
    }

    // PATHOLOGY #3 - Frames out of sequence
    {
        cout << endl;
        cout << "===== Test 3 ==========================================" << endl;
        cout << endl;

        std::vector<TestFrame> outs;
        TestSink2 sink(outs);
        SequencingBufferStd<TestFrame> jb;
        jb.setInitialMargin(40);
        jb.lockDelay();
        std::vector<TestFrame> ins;
        ins.push_back({ .origMs = 20, .rxMs = 120, .voice=true });
        ins.push_back({ .origMs = 60, .rxMs = 160, .voice=true });
        ins.push_back({ .origMs = 40, .rxMs = 162, .voice=true });

        play(log, 20, 300, ins, jb, &sink, false, 
            [&](uint32_t t) {
                // Since the initial margin is 40 and the first packet arrives at 120,
                // we're not going to see any output until 160
                if (t < 160) {
                    assert(outs.empty());
                }
                else if (t == 160) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 20);
                    outs.clear();
                }
                else if (t == 180) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 40);
                    outs.clear();
                }
                else if (t == 200) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 60);
                    outs.clear();
                }
            }
        );
        assert(sink.voiceCount == 3);
        assert(sink.interpolateCount == 4);
    }

    // PATHOLOGY #4 - Halt and then a flood to catch up.
    // We should re-synchronize on the beginning of the flood so that nothing
    // is lost.
    {
        cout << endl;
        cout << "===== Test 4 ==========================================" << endl;
        cout << endl;

        std::vector<TestFrame> outs;
        TestSink2 sink(outs);
        SequencingBufferStd<TestFrame> jb;
        jb.setInitialMargin(40);
        jb.lockDelay();
        std::vector<TestFrame> ins;
        ins.push_back({ .origMs = 20, .rxMs = 120, .voice=true });
        ins.push_back({ .origMs = 40, .rxMs = 140, .voice=true });
        // Starting here there is a 100ms gap with nothing received,
        // and then a flood of 5 frames at 240 to resume.
        ins.push_back({ .origMs = 60, .rxMs = 240, .voice=true });
        ins.push_back({ .origMs = 80, .rxMs = 240, .voice=true });
        ins.push_back({ .origMs = 100, .rxMs = 240, .voice=true });

        play(log, 20, 300, ins, jb, &sink, true, 
            [&](uint32_t t) {
                // Since the initial margin is 40 and the first packet arrives at 120,
                // we're not going to see any output until 160
                if (t < 160) {
                    assert(outs.empty());
                }
                else if (t == 160) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 20);
                    outs.clear();
                }
                else if (t == 180) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 40);
                    outs.clear();
                }
                // 2 interpolations while waiting for recovery
                else if (t == 200 || t == 220) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 0);
                    outs.clear();
                }
                // Re-synchronize delay and pick up as quickly as possible
                else if (t == 240) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 60);
                    outs.clear();
                }
                else if (t == 260) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 80);
                    outs.clear();
                }
                else if (t == 280) {
                    assert(outs.size() == 1);
                    assert(outs.front().origMs == 100);
                    outs.clear();
                }
            }
        );
        assert(sink.voiceCount == 5);
        assert(sink.interpolateCount == 2);
    }

    // PATHOLOGY #5 - Duplicate frame
    {
        cout << endl;
        cout << "===== Test 5 ==========================================" << endl;
        cout << endl;

        std::vector<TestFrame> outs;
        TestSink2 sink(outs);
        SequencingBufferStd<TestFrame> jb;
        jb.setInitialMargin(20);
        jb.lockDelay();
        std::vector<TestFrame> ins;
        ins.push_back({ .origMs = 20, .rxMs = 40, .voice=true });
        // Duplicate, will be discarded
        ins.push_back({ .origMs = 20, .rxMs = 60, .voice=true });
        ins.push_back({ .origMs = 40, .rxMs = 80, .voice=true });
        ins.push_back({ .origMs = 60, .rxMs = 100, .voice=true });
        ins.push_back({ .origMs = 80, .rxMs = 120, .voice=true });
        ins.push_back({ .origMs = 100, .rxMs = 140, .voice=true });

        play(log, 20, 300, ins, jb, &sink, true, 
            [&](uint32_t t) {
            }
        );
        assert(sink.voiceCount == 5);
        assert(sink.interpolateCount == 4);
    }
}

void test_2() {

    Log log;

    // A very straight-forward example, demonstrates that the initial margin
    // was used properly.
    {
        cout << endl;
        cout << "===== Test 0a ==========================================" << endl;
        cout << endl;

        TestSink3 sink;
        SequencingBufferStd<Message> jb;
        jb.setInitialMargin(20);
        jb.lockDelay();

        jb.playOut(log, 80, &sink);
        jb.playOut(log, 100, &sink);
        jb.consume(log, Message(Message::Type::AUDIO, 0, 0, 0, 20, 120000));
        jb.playOut(log, 120, &sink);
        jb.consume(log, Message(Message::Type::AUDIO, 0, 0, 0, 40, 140000));
        jb.playOut(log, 140, &sink);
        jb.consume(log, Message(Message::Type::AUDIO, 0, 0, 0, 60, 160000));
        jb.playOut(log, 160, &sink);
        jb.playOut(log, 180, &sink);
        jb.playOut(log, 200, &sink);
        assert(sink.voiceCount == 3);
        assert(sink.interpolateCount == 1);
    }
}

int main(int, const char**) {
    test_1();
    test_2();
}
