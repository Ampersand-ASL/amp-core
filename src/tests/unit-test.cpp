#include <iostream>
#include <cmath> 
#include <ctime>
#include <fstream>
#include <cassert>
#include <ctime>
#include <unistd.h>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/StdPollTimer.h"
#include "kc1fsz-tools/StdPollTimer.h"
#include "kc1fsz-tools/linux/StdClock.h"
#include "kc1fsz-tools/fixedqueue.h"
#include "kc1fsz-tools/NetUtils.h"
#include "kc1fsz-tools/fixed_math.h"

#include "itu-g711-codec/codec.h"
#include "amp/Resampler.h"
#include "amp/SequencingBufferStd.h"

#include "Message.h"
#include "IAX2FrameFull.h"
#include "BridgeCall.h"
#include "Bridge.h"
#include "NullConsumer.h"
#include "TestUtil.h"
#include "dsp_util.h"

using namespace std;
using namespace kc1fsz;

static void bufferTest1() {
    uint8_t packet[32] = { 1, 4, 1, 2, 3, 4, 2, 2, 1, 1, 4, 0 };
    uint8_t buf[32];
    assert(extractIE(packet, 10, 1, buf, 32) == 4);
    assert(extractIE(packet, 10, 2, buf, 32) == 2);
    assert(extractIE(packet, 12, 4, buf, 32) == 0);
}

static void timerTest1() {

    StdClock clock;
    StdPollTimer timer2ms(clock, 20000);

    timer2ms.reset();
    // Wait until the next 20ms tick to get aligned
    while (!timer2ms.poll()) {
    }
    timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t startus = t1.tv_sec * 1000000 + t1.tv_nsec / 1000;
    // Wait until the 20ms tick
    while (!timer2ms.poll()) {
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t endus = t1.tv_sec * 1000000 + t1.tv_nsec / 1000;
    uint64_t diff = endus - startus;
    assert(diff > 19000 && diff < 21000); 

    // Show that the time stays constant until the poll rolls it
    uint64_t p0 = timer2ms.getCurrentIntervalUs();
    sleep(1);
    uint64_t p1 = timer2ms.getCurrentIntervalUs();
    // Even though we slept for a full second, the time is still the same
    assert(p0 == p1);
    assert(timer2ms.poll());
    uint64_t p2 = timer2ms.getCurrentIntervalUs();
    diff = p2 - p0;
    // Even though we slept for a whole second, the time has only advanced by 20ms
    assert(diff > 19000 && diff < 21000); 
}

static void timerTest2() {

    StdClock clock;
    StdPollTimer timer20ms(clock, 20000);

    timer20ms.reset();
    // Wait until the next 20ms tick to get aligned
    while (!timer20ms.poll()) {
    }
    // Now that the poll has happened the time left should be large
    assert(timer20ms.usLeftInInterval() > 19000);

    timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t startus = t1.tv_sec * 1000000 + t1.tv_nsec / 1000;
    // Wait until the 20ms tick
    while (!timer20ms.poll()) {
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t endus = t1.tv_sec * 1000000 + t1.tv_nsec / 1000;
    uint64_t diff = endus - startus;
    assert(diff > 19000 && diff < 21000); 

    // Show that the time stays constant until the poll rolls it
    uint64_t p0 = timer20ms.getCurrentIntervalUs();
    sleep(1);
    uint64_t p1 = timer20ms.getCurrentIntervalUs();
    // Even though we slept for a full second, the time is still the same
    assert(p0 == p1);
    // Just before the poll the time left should be nothing
    assert(timer20ms.usLeftInInterval() == 0);
    assert(timer20ms.poll());
    uint64_t p2 = timer20ms.getCurrentIntervalUs();
    diff = p2 - p0;
    // Even though we slept for a whole second, the time has only advanced by 20ms
    assert(diff > 19000 && diff < 21000); 
}

static void clockTest1() {
    StdClock clock;
    uint32_t a = clock.time();
    sleep(1);
    uint32_t b = clock.time();
    uint32_t diff = b - a;
    assert(diff > 990 && diff < 1100);
}

static void frameTest1() {
    
    Message frame0(Message::Type::AUDIO, 1, 2, (uint8_t*)"1", 0, 0);
    Message frame1(Message::Type::TEXT, 1, 2, (uint8_t*)"1", 0, 0);

    Message flSpace[4];
    fixedqueue<Message> fl(flSpace, 4);

    fl.push(frame0);
    fl.push(frame1);
    assert(fl.size() == 2);
    fl.pop();
    assert(fl.size() == 1);
    fl.pop();
    assert(fl.size() == 0);
}

/*
static void dspTest1() {

    // Initialize the filters
    arm_fir_instance_q15 f1Filter;
    int16_t f1State[ChannelUsb::F1_TAPS + ChannelUsb::BLOCK_SIZE_48K - 1];
    arm_fir_init_q15(&f1Filter, ChannelUsb::F1_TAPS, 
        ChannelUsb::F1_COEFFS, f1State, ChannelUsb::BLOCK_SIZE_48K);
    arm_fir_instance_q15 f2Filter;
    int16_t f2State[ChannelUsb::F2_TAPS + ChannelUsb::BLOCK_SIZE_48K - 1];
    arm_fir_init_q15(&f2Filter, ChannelUsb::F2_TAPS, 
        ChannelUsb::F2_COEFFS, f2State, ChannelUsb::BLOCK_SIZE_48K);

    unsigned seconds = 2;
    unsigned frames = seconds * 8000 / 160;
    unsigned sampleRate = 8000;
    float ft = 2000;
    const float omega = ft * 2.0 * 3.1415926 / (float)sampleRate;
    float phi = 0;

    ofstream outFile("dsp1.txt");

    for (unsigned f = 0; f < frames; f++) {

        // Make an 8k G711 frame
        uint8_t buffer[160];
        for (unsigned i = 0; i < 160; i++) {
            float a = (32767.0) * 0.99 * std::sin(phi);
            buffer[i] = encode_ulaw((int16_t)a);
            phi += omega;
        }

        // Convert to pcm 48
        int16_t pcm48[960];
        ChannelUsb::g711ToPcm48(&f1Filter, buffer, 160, pcm48, 960);

        for (unsigned i = 0; i < 960; i++)
            outFile << pcm48[i] << endl;
    }
}
*/

void pack1() {
    uint8_t d[] = { 0x01, 0x02 };
    assert(unpack_int16_le(d) == 0x0201);
}

static void resampler_1() {
    amp::Resampler resampler;
}

static void testRound() {
    assert(amp::SequencingBufferStd<Message>::roundDownToTick(6755, 20) == 6740);
    assert(amp::SequencingBufferStd<Message>::roundDownToTick(6752, 20) == 6740);
    assert(amp::SequencingBufferStd<Message>::roundDownToTick(6740, 20) == 6740);
    assert(amp::SequencingBufferStd<Message>::roundDownToTick(6779, 20) == 6760);
}

static void sizeCheck1() {
    cout << "Message          " << sizeof(Message) << endl;
    cout << "Message x 64     " << sizeof(Message) * 64 << endl;
    cout << "SequencingBuffer " << sizeof(amp::SequencingBufferStd<Message>) << endl;
    cout << "BridgeIn         " << sizeof(amp::BridgeIn) << endl;
    cout << "BridgeOut        " << sizeof(BridgeOut) << endl;
    cout << "BridgeCall       " << sizeof(amp::BridgeCall) << endl;
}

static void snprintfCheck() {
    char target[8];
    // NOTE: This line will generate a compiler warning normally
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(target, 8, "%s", "01234567");
#pragma GCC diagnostic pop
    // Notice that the result was shortened and null-terminated
    assert(strlen(target) == 7);
    assert(target[7] == 0);
}

static void fixedMath1() {

    int16_t a = 0.5f * 32767.0f;
    float scale = 0.5;
    int16_t scaleFixed = scale * 32767.0;

    // Existing way
    int16_t b = (float)a * scale;
    // Fixed way
    int16_t c = mult(a, scaleFixed);
    assert(b == c);

    scale = 1.0f / 3.0f;
    scaleFixed = scale * 32767.0;
    // Existing way
    b = (float)a * scale;
    // Fixed way
    c = mult(a, scaleFixed);
    // It will be close
    assert(abs(b - c) < 2);

    scale = 1.0f / 250.0f;
    scaleFixed = scale * 32767.0;
    // Existing way
    b = (float)a * scale;
    // Fixed way
    c = mult(a, scaleFixed);
    // It will be close
    assert(abs(b - c) < 2);

    // Reciprocal test
    scale = 1.0f / 2.0f;
    scaleFixed = scale * 32767.0;
    int16_t recip = 0x7fff / 3;
    // Fixed way
    c = mult(recip, scaleFixed);
}

static void speedTest1() {

    Log log;
    StdClock clock;
    LogConsumer nullCons([](const Message& frame) {
        /*
        if (frame.getDestCallId() == 21) {
            // FFT analysis
            cf32 fftIn[160];
            cf32 fftOut[160];
            const uint8_t* p = frame.body();
            for (unsigned i = 0; i < 160; i++, p += 2) {
                int16_t sample = unpack_int16_le(p);
                fftIn[i] = cf32((float)sample / 32767.0, 0);
            }
            simpleDFT(fftIn, fftOut, 160);
            int gotBucket = maxMagIdx(fftOut, 0, 80);
            cout << "Output freq/mag " << 8000 * gotBucket / 160  << " " 
                << fftOut[gotBucket].mag() << endl;
            cout << "  -1 " << fftOut[gotBucket - 1].mag() << endl;
            cout << "     " << fftOut[gotBucket].mag() << endl;
            cout << "  +1 " << fftOut[gotBucket + 1].mag() << endl;
        }
        */
    });

    unsigned bridgeLineId = 10;
    amp::Bridge bridge(log, log, clock, nullCons,  amp::BridgeCall::Mode::NORMAL,
        bridgeLineId, 0, 0, 0, 1);
    bridge.setLocalNodeNumber("1000");

    unsigned lineId = 1;

    // Start some calls
    unsigned callCount = 100;
    for (unsigned i = 0; i < callCount; i++) {
        unsigned callId = 20 + i;
        PayloadCallStart payload;
        payload.codec = CODECType::IAX2_CODEC_SLIN_8K;
        payload.bypassJitterBuffer = true;
        payload.startMs = clock.time();
        strcpyLimited(payload.localNumber, "1000", sizeof(payload.localNumber));
        strcpyLimited(payload.remoteNumber, "2000", sizeof(payload.remoteNumber));
        payload.originated = false;
        Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
            sizeof(payload), (const uint8_t*)&payload, 0, clock.time());
        msg.setSource(lineId, callId);
        msg.setDest(bridgeLineId, Message::UNKNOWN_CALL_ID);
        bridge.consume(msg);
    }

    // Generate some audio 
    uint8_t audio[320];
    float sampleRate = 8000;
    float omega = 2.0f * 3.1415926f * 400.0f / sampleRate;
    float phi = 0;
    uint8_t* p = audio;
    for (unsigned i = 0; i < 160; i++, p += 2) {
        int16_t sample = std::cos(phi) * 32767.0;
        pack_int16_le(sample, p);
        phi += omega;
    }

    // FFT analysis
    {
        cf32 fftIn[160];
        cf32 fftOut[160];
        p = audio;
        for (unsigned i = 0; i < 160; i++, p += 2) {
            int16_t sample = unpack_int16_le(p);
            fftIn[i] = cf32((float)sample / 32767.0, 0);
        }
        simpleDFT(fftIn, fftOut, 160);
        int gotBucket = maxMagIdx(fftOut, 0, 80);
        cout << "Input freq/mag " << 8000 * gotBucket / 160  << " " 
            << fftOut[gotBucket].mag() << endl;
    }

    Message voice(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_8K, 
        160 * 2, audio, 1000, 1000);
    voice.setSource(lineId, 20);
    voice.setDest(bridgeLineId, Message::UNKNOWN_CALL_ID);
    bridge.consume(voice);

    nullCons.reset();
    uint64_t startUs = clock.timeUs();
    // Tick
    bridge.audioRateTick(1000);
    uint64_t endUs = clock.timeUs();
    cout << "Time " << (endUs - startUs) << endl;
    cout << "Count " << nullCons.getCount() << endl;

    //bridge.audioRateTick(1020);
    //bridge.audioRateTick(1040);
}

int main(int, const char**) {
    speedTest1();
    fixedMath1();
    sizeCheck1();
    snprintfCheck();
    testRound();
    resampler_1();
    pack1();
    bufferTest1();
    clockTest1();
    timerTest1();
    timerTest2();
    frameTest1();
    //dspTest1();
}
