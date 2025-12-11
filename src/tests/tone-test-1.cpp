#include <iostream>
#include <alsa/asoundlib.h>
#include <cmath> 

#include <itu-g711-codec/codec.h>
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/LinuxPollTimer.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "ChannelUsb.h"
#include "NullConsumer.h"

using namespace std;
using namespace kc1fsz;

static const char *alsaDeviceName = "default"; // Or "hw:0,0", etc.
static const char* usbHidName = "/dev/hidraw0";

int main(int, const char**) {

    Log log;
    log.info("Start");

    StdClock clock;
    NullConsumer null;
    ChannelUsb radio0(log, clock, null, 0);
    int rc = radio0.open(alsaDeviceName, usbHidName);
    if (rc < 0)
        log.error("%d", rc);

    LinuxPollTimer timer2ms(20000);

    const unsigned sampleRate = 8000;
    const unsigned frames = 5 * sampleRate / 160;
    const float omega = 440.0 * 2.0 * 3.1415926 / (float)sampleRate;
    float phi = 0;

    //timespec t1;
    //clock_gettime(CLOCK_MONOTONIC, &t1);
    //cout << t1.tv_sec << "," << t1.tv_nsec / 1000000 << endl;
    
    timer2ms.reset();

    for (unsigned f = 0; f < frames; f++) {
        // Wait until the 2ms tick
        while (!timer2ms.poll()) {
        }
        //timespec t1;
        //clock_gettime(CLOCK_MONOTONIC, &t1);
        //cout << t1.tv_sec << "," << t1.tv_nsec / 1000000 << endl;
        // Make an 8k G711 frame
        uint8_t buffer[160];
        for (unsigned i = 0; i < 160; i++) {
            float a = (32767.0) * 0.9 * std::cos(phi);
            buffer[i] = encode_ulaw((int16_t)a);
            phi += omega;
        }
        Frame outFrame(0, Frame::Type::AUDIO, Frame::Enc::G711_ULAW, 160, buffer);
        radio0.consume(outFrame);
        if (f > 1)
            radio0.audioRateTick();
    }

    radio0.close();

    log.info("Done");

    return 0;
}
