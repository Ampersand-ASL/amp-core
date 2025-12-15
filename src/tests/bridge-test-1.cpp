#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "Bridge.h"

using namespace std;
using namespace kc1fsz;


class TestSink : public MessageConsumer {
public:

    void consume(const Message& msg) {
        cout << "Dest line/call " << msg.getDestBusId() << "/" << msg.getDestCallId() << endl;
        int16_t* r = (int16_t*)msg.body();
        cout << "  DC " << r[0] << endl;
    }
};

int main(int, const char**) {
    
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

    // Send in some audio
    {
        int16_t audio[960];
        for (unsigned i = 0;i < 960; i++)
            audio[i] = 0.5f * 32767.0f;
        Message audioIn(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K,
            960 * 2, (const uint8_t*)audio, 0, 0);
        audioIn.setSource(10, 1);
        audioIn.setDest(1, 0);
        bridge.consume(audioIn);

        audioIn.setSource(10, 2);
        bridge.consume(audioIn);
    }

    // Tick
    bridge.audioRateTick();

    cout << "hello izzy" << endl;
}
