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

#include "itu-g711-codec/codec.h"
#include "amp/Resampler.h"
#include "amp/SequencingBufferStd.h"

#include "Message.h"
#include "IAX2FrameFull.h"
#include "BridgeCall.h"

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

int main(int, const char**) {
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
