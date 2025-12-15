#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "Bridge.h"

using namespace std;
using namespace kc1fsz;

class TestSink : public MessageConsumer {
public:

    void consume(const Message& msg) {
        //cout << "Dest line/call " << msg.getDestBusId() << "/" << msg.getDestCallId() << endl;
        if (msg.getDestBusId() == 10 && msg.getDestCallId() == 1)
            _out1 = msg;
        else if (msg.getDestBusId() == 10 && msg.getDestCallId() == 2)
            _out2 = msg;
        else if (msg.getDestBusId() == 10 && msg.getDestCallId() == 3)
            _out3 = msg;
        else 
            assert(false);
    }

    Message _out1;
    Message _out2;
    Message _out3;
};

bool closeTo(int16_t value, int16_t target) {
    return (target - 2 < value && value < target + 2);
}

static void test_1() {
    
    Log log;
    StdClock clock;
    amp::Bridge bridge(log, clock);
    TestSink sink; 
    bridge.setSink(&sink);

    // Start a few calls
    {
        PayloadCallStart cs;
        cs.codec = CODECType::IAX2_CODEC_G711_ULAW;
        cs.startMs = 100;
        Message msg0(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
            sizeof(cs), (const uint8_t*)&cs, 0, 0);
        msg0.setSource(10, 1);
        msg0.setDest(1, 0);
        bridge.consume(msg0);
    }
    {
        PayloadCallStart cs;
        cs.codec = CODECType::IAX2_CODEC_G711_ULAW;
        cs.startMs = 100;
        Message msg0(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
            sizeof(cs), (const uint8_t*)&cs, 0, 0);
        msg0.setSource(10, 2);
        msg0.setDest(1, 0);
        bridge.consume(msg0);
    }
    {
        PayloadCallStart cs;
        cs.codec = CODECType::IAX2_CODEC_G711_ULAW;
        cs.startMs = 100;
        Message msg0(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
            sizeof(cs), (const uint8_t*)&cs, 0, 0);
        msg0.setSource(10, 3);
        msg0.setDest(1, 0);
        bridge.consume(msg0);
    }

    // Send in some audio at two different levels
    {
        int16_t audio[960];
        for (unsigned i = 0;i < 960; i++)
            audio[i] = 0.5f * 32767.0f;
        Message audioIn1(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K,
            960 * 2, (const uint8_t*)audio, 0, 0);
        audioIn1.setSource(10, 1);
        audioIn1.setDest(1, 0);
        bridge.consume(audioIn1);

        for (unsigned i = 0;i < 960; i++)
            audio[i] = 0.25f * 32767.0f;
        Message audioIn2(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K,
            960 * 2, (const uint8_t*)audio, 0, 0);
        audioIn2.setSource(10, 2);
        bridge.consume(audioIn2);
    }

    // Tick
    bridge.audioRateTick();

    // Make sure the audio got scaled properly
    int16_t* pcm1 = (int16_t*)sink._out1.body();
    assert(closeTo(pcm1[0], 0.25f * 32767.0f));
    int16_t* pcm2 = (int16_t*)sink._out2.body();
    assert(closeTo(pcm2[0], 0.5f * 32767.0f));
    int16_t* pcm3 = (int16_t*)sink._out3.body();
    assert(closeTo(pcm3[0], 0.375f * 32767.0f));
}

int main(int, const char**) {
    test_1();
}
