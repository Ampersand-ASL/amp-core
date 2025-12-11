/**
 * Copyright (C) 2025, Bruce MacKinnon KC1FSZ
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>
#include <alsa/asoundlib.h>

#include <cmath> 
#include <algorithm>

#include "itu-g711-codec/codec.h"
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/LinuxPollTimer.h"
#include "kc1fsz-tools/linux/StdClock.h"
#include "kc1fsz-tools/fixedqueue.h"
#include "sound-map.h"

#include "LineUsb.h"
#include "Message.h"
#include "MessageConsumer.h"
#include "EventLoop.h"
#include "MessageBus.h"
#include "NodeParrot.h"

using namespace std;
using namespace kc1fsz;

/*
Development:

export AMP_NODE0_USBSOUND="vendorname:\"C-Media Electronics, Inc.\""
*/

// An adaptor that takes frames and pushes them onto the queue
// provided.
class Signaler : public MessageConsumer {
public:

    Signaler(Clock& clock)
    :   _clock(clock) { }

    void start() {
        if (!_used) {
            PayloadCallStart payload;
            payload.codec = CODECType::IAX2_CODEC_SLIN_48K;
            Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
                sizeof(payload), (const uint8_t*)&payload,
                _clock.timeUs());
            msg.setSource(1, 6);
            sink->consume(msg);
            _used = true;
        }
    }

    void consume(const Message& frame) {
        sink->consume(frame);
    }

    MessageConsumer* sink = 0;

private:

    Clock& _clock;
    bool _used = false;
};

static void sigHandler(int sig) {
    void *array[32];
    // get void*'s for all entries on the stack
    size_t size = backtrace(array, 32);
    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    // Now do the regular thing
    signal(sig, SIG_DFL); 
    raise(sig);
}

/*
Topology:
radio0 -> bus0 -> parrot0
parrot0 -> bus1 -> radio0
*/
int main(int, const char**) {
    
    signal(SIGSEGV, sigHandler);

    Log log;
    log.info("Start");
    StdClock clock;

    Signaler bus0(clock);
    MessageBus bus1;

    // Resolve the sound card/HID name
    char alsaCardNumber[16];
    char hidDeviceName[32];
    int rc2 = querySoundMap(getenv("AMP_NODE0_USBSOUND"), 
        hidDeviceName, 32, alsaCardNumber, 16, 0, 0);
    if (rc2 < 0) {
        log.error("Unable to resolve USB device");
        return -1;
    }
    char alsaDeviceName[32];
    snprintf(alsaDeviceName, 32, "plughw:%s", alsaCardNumber);
    log.info("USB %s mapped to %s, %s", getenv("AMP_NODE0_USBSOUND"),
        hidDeviceName, alsaDeviceName);

    LineUsb radio0(log, clock, bus0, 1, 6, 7777, 0);
    int rc = radio0.open(alsaDeviceName, hidDeviceName);
    if (rc < 0) {
        log.error("%d", rc);
        return -1;
    }

    NodeParrot parrot0(log, clock, bus1);

    bus0.sink = &parrot0;
    bus1.targetChannel = &radio0;
    bus0.start();
    
    // Main loop    
    Runnable2* tasks2[16] = { &radio0, &parrot0 };
    unsigned task2Count = 2;

    EventLoop::run(log, clock, 0,0, tasks2, task2Count);

    radio0.close();

    log.info("Done");

    return 0;
}

