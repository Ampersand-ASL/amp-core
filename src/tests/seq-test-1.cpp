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

    virtual void playSignal(const int& payload, uint32_t localTime) {
        _outs.push_back({ .rxTime=localTime, .remoteTime=0, 
            .type=Type::SIGNAL, .token=payload });
        cout << "Release signal: t=" << localTime << " payload=" << payload << endl;
        signalCount++;
    };

    virtual void playVoice(const int& payload, uint32_t localTime) {
        _outs.push_back({ .rxTime=localTime, .remoteTime=0, 
            .type=Type::VOICE, .token=payload });
        cout << "Release voice: t=" << localTime << " payload=" << (long)payload << endl;
        voiceCount++;
    };

    virtual void interpolateVoice(uint32_t localTime, uint32_t duration) {
        _outs.push_back({ .rxTime=localTime, .remoteTime=0, 
            .type=Type::VOICE_INTERPOLATE });
        cout << "Interpolate voice: t=" << localTime << endl;
        interpolateCount++;
    };

private:

    vector<TestInput>& _outs;
};

void test_1() {

    cout << endl;
    cout << "===== Test 1 ==========================================" << endl;
    cout << endl;

    Log log;
    std::vector<TestInput> outs;
    TestSink2 sink(outs);
    SequencingBufferStd<int> jb;
    jb.setInitialMargin(20);
    jb.lockDelay();
 
    bool b;

    // payload, remote, local
    b = jb.consumeSignal(log, 2, 20, 10);
    assert(b);

    // rt=20
    // A second message out of order, but unique
    b = jb.consumeSignal(log, 1, 20, 10);
    assert(b);
    assert(jb.size() == 2);

    // rt=40
    b = jb.consumeSignal(log, 3, 40, 30);
    assert(b);
    assert(jb.size() == 3);

    // rt=60, lt=100. This voice should set the flight time to 40 and the 
    // delay to 60.
    jb.consumeVoice(log, 4, 60, 100);
    assert(jb.size() == 4);

    // rt=60
    // A signal frame with the same timestamp
    b = jb.consumeSignal(log, 5, 60, 100);
    assert(b);
    assert(jb.size() == 5);

    cout << "t=100" << endl;

    // All of the signal frames before the first voice frame will be released
    jb.playOut(log, 100, &sink);
    assert(jb.size() == 2);
    assert(sink.signalCount == 3);
    assert(sink.voiceCount == 0);

    cout << "t=120" << endl;

    // This happens at t=140. 
    jb.playOut(log, 120, &sink);
    assert(jb.size() == 1);
    assert(sink.signalCount == 3);
    assert(sink.voiceCount == 1);

    cout << "t=140" << endl;

    // Now that the voice frame has released, the signal will come out.
    // Since we are missing a voice frame here we will see interpolation.
    jb.playOut(log, 140, &sink);
    assert(jb.size() == 0);
    assert(sink.signalCount == 4);
    assert(sink.voiceCount == 1);
    assert(sink.interpolateCount == 1);
    assert(jb.inTalkspurt());
}

void test_2() {

    Log log;

    cout << endl;
    cout << "===== Test 2 ==========================================" << endl;
    cout << endl;

    unsigned gapT = 20;
    unsigned lastT = 340;

    // In this test we send in a series of voice mini frames that arrive 
    // with slight jitter.
    std::vector<TestInput> ins;
    // This one will get played at t=40 
    ins.push_back({ .rxTime= 10, .remoteTime=  0, .type=Type::VOICE, .token=0 });
    // This one will get played at t=60
    ins.push_back({ .rxTime= 25, .remoteTime= 20, .type=Type::VOICE, .token=1 });
    ins.push_back({ .rxTime= 26, .remoteTime= 21, .type=Type::SIGNAL, .token=100 });
    // Will be played at t=80
    ins.push_back({ .rxTime= 48, .remoteTime= 40, .type=Type::VOICE, .token=2 });
    // Will be played at t=100
    ins.push_back({ .rxTime= 65, .remoteTime= 60, .type=Type::VOICE, .token=3 });
    // Out of sequence
    // This one should be played at t=140
    ins.push_back({ .rxTime= 110, .remoteTime=100, .type=Type::VOICE, .token=5 });
    // This one should be played at t=120. (The arrival time is just before)
    ins.push_back({ .rxTime= 119, .remoteTime= 80, .type=Type::VOICE, .token=4 });
    // This one will get played at t=160 
    // This will be the last audio, so the talkspurt will timeout after t=220.
    // That means t=180 and t=200 will be interpolated
    ins.push_back({ .rxTime= 125, .remoteTime=120, .type=Type::VOICE, .token=6 });
    // This one is too late for the first talkspurt, but it will start a new
    // one.
    ins.push_back({ .rxTime= 201, .remoteTime=140, .type=Type::VOICE, .token=7 });

    // Sort the vector by receive time
    std::sort(ins.begin(), ins.end(), [](const TestInput& a, const TestInput& b) {
        return a.rxTime < b.rxTime;
    });

    std::vector<TestInput> outs;

    TestSink2 sink(outs);
    SequencingBufferStd<int> jb;
    jb.setInitialMargin(0);
    jb.lockDelay();
    jb.setTalkspurtTimeoutInterval(40);

    play(log, gapT, lastT, ins, jb, &sink);

    // Validate some of the metrics
    assert(jb.empty());
    assert(jb.getLateVoiceFrameCount() == 1);
    assert(jb.getInterpolatedVoiceFrameCount() == 9);
    assert(sink.voiceCount == 7);
    assert(sink.signalCount == 1);
}

void test_3() {

    Log log;

    cout << endl;
    cout << "==== Test 3 ===========================================" << endl;
    cout << endl;

    // In this test we send in three voice frames that arrive 
    // at about the same time (and out of order), but the sequence 
    // buffer will space out their release.

    std::vector<TestInput> outs;
    TestSink2 sink(outs);
    SequencingBufferStd<int> jb;
    jb.setInitialMargin(20);
    jb.lockDelay();

    // Payload, remoteTime, localTime
    jb.consumeVoice(log, 0, 0, 10);
    cout << "Playout t=20" << endl;
    jb.playOut(log, 20, &sink);
    assert(jb.empty());

    jb.consumeVoice(log, 1, 20, 25);

    // This is completely out of order (arrived too early), will
    // be played when t=80
    jb.consumeVoice(log, 3, 60, 35);

    cout << "Playout t=40" << endl;
    jb.playOut(log, 40, &sink);
    assert(!jb.empty());
    
    jb.consumeVoice(log, 2, 40, 36);

    cout << "Playout t=60" << endl;
    jb.playOut(log, 60, &sink);
    assert(!jb.empty());

    cout << "Playout t=80" << endl;
    jb.playOut(log, 80, &sink);
    assert(!jb.empty());
}

void test_4() {

    cout << endl;
    cout << "==== Test 4 ===========================================" << endl;
    cout << endl;

    Log log;

    unsigned gapT = 20;
    unsigned lastT = 7000;

    // Flood test - 100 frames all received at once.
    // The difference between local time and remote
    // time is still small enough to allow the frames
    // into the buffer.
    std::vector<TestInput> ins;
    for (unsigned t = 0; t < 100; t++)
        ins.push_back({ .rxTime=4000, .remoteTime=t * 20, 
            .type=Type::VOICE, .token=0 });

    std::vector<TestInput> outs;

    TestSink2 sink(outs);
    SequencingBufferStd<int> jb;
    jb.setInitialMargin(40);
    jb.lockDelay();
    jb.setTalkspurtTimeoutInterval(40);
    play(log, gapT, lastT, ins, jb, &sink, false);

    // Since all of the entries showed up and once, a lot of them would
    // be considered overflow
    assert(jb.getOverflowCount() == 100 - jb.maxSize());
}

void test_5() {

    Log log;

    cout << endl;
    cout << "==== Test 5 ===========================================" << endl;
    cout << endl;

    std::vector<TestInput> outs;
    TestSink2 sink(outs);

    {
        cout << "----- No Jitter Case ----------" << endl;

        SequencingBufferStd<int> jb;
        jb.setInitialMargin(20);
        jb.lockDelay();

        // payload, remote, local
        // Notice no jitter
        jb.consumeVoice(log, 0, 0, 20);
        jb.consumeVoice(log, 0, 20, 40);
        jb.consumeVoice(log, 0, 40, 60);
        jb.consumeVoice(log, 0, 60, 80);

        jb.playOut(log, 20, &sink);
        jb.playOut(log, 40, &sink);
        jb.playOut(log, 60, &sink);
        jb.playOut(log, 80, &sink);
        jb.playOut(log, 100, &sink);
        jb.playOut(log, 120, &sink);
        jb.playOut(log, 140, &sink);
        jb.playOut(log, 160, &sink);
        jb.playOut(log, 180, &sink);
        jb.playOut(log, 200, &sink);

        // Playing >4 ticks, only received 4 voice frames
        assert(sink.interpolateCount == 4);
    }

    // Here we introduce some jitter with an upwards bias
    {
        cout << "----- Upward Jitter Case ----------" << endl;

        SequencingBufferStd<int> jb;
        jb.setInitialMargin(20);
        jb.lockDelay();

        // payload, remote, local
        jb.consumeVoice(log, 0, 0, 20);
        jb.consumeVoice(log, 0, 20, 45);
        jb.consumeVoice(log, 0, 40, 58);
        jb.consumeVoice(log, 0, 60, 81);

        jb.playOut(log, 20, &sink);
        jb.playOut(log, 40, &sink);
        jb.playOut(log, 60, &sink);
        jb.playOut(log, 80, &sink);
        jb.playOut(log, 100, &sink);
        jb.playOut(log, 120, &sink);
    }


    {
        cout << "----- No Jitter, Floating Delay Case ----------" << endl;

        SequencingBufferStd<int> jb;
        std::vector<TestInput> outs;
        TestSink2 sink(outs);

        // payload, remote, local
        // Notice no jitter
        jb.consumeVoice(log, 0, 0, 20);
        jb.consumeVoice(log, 0, 20, 40);
        jb.consumeVoice(log, 0, 40, 60);
        jb.consumeVoice(log, 0, 60, 80);

        jb.playOut(log, 20, &sink);
        jb.playOut(log, 40, &sink);
        jb.playOut(log, 60, &sink);
        jb.playOut(log, 80, &sink);
        jb.playOut(log, 100, &sink);
        jb.playOut(log, 120, &sink);
        jb.playOut(log, 140, &sink);
        jb.playOut(log, 160, &sink);
        jb.playOut(log, 180, &sink);
        jb.playOut(log, 200, &sink);

        // Playing >4 ticks, only received 4 voice frames
        assert(sink.interpolateCount == 4);
    }

    // Here we introduce some jitter with a downwards bias
    {
        cout << "----- Downward Jitter, Floating Delay Case ----------" << endl;

        SequencingBufferStd<int> jb;
        std::vector<TestInput> outs;
        TestSink2 sink(outs);

        // payload, remote, local
        jb.consumeVoice(log, 0, 0, 20);
        jb.consumeVoice(log, 0, 20, 35);
        jb.consumeVoice(log, 0, 40, 60);
        jb.consumeVoice(log, 0, 60, 79);

        jb.playOut(log, 20, &sink);
        jb.playOut(log, 40, &sink);
        jb.playOut(log, 60, &sink);
        jb.playOut(log, 80, &sink);
        jb.playOut(log, 100, &sink);
        jb.playOut(log, 120, &sink);
        jb.playOut(log, 140, &sink);
        jb.playOut(log, 160, &sink);
        jb.playOut(log, 180, &sink);
        jb.playOut(log, 200, &sink);

    }
}

void test_6() {
    Message m(Message::Type::AUDIO, 0, 4, (const uint8_t*)"test", 0);
    m.setSource(6,6);
    assert(m.getSourceCallId() == 6);
    std::queue<Message> q;
    q.push(m);
    Message m0 = q.front();
    assert(m0.getSourceCallId() == 6);
}

int main(int,const char**) {
    // remote, local
    assert(SequencingBufferStd<int>::extendTime(0x0001, 0x00020001) == 0x00020001);
    assert(SequencingBufferStd<int>::extendTime(0x0001, 0x0001ffff) == 0x00020001);
    assert(SequencingBufferStd<int>::extendTime(0xffff, 0x00020001) == 0x0001ffff);
    assert(SequencingBufferStd<int>::extendTime(0xffff, 0x0002fffe) == 0x0002ffff);

    test_1();
    //test_2();
    //test_3();
    //test_4();
    //test_5();
    //test_6();
}
