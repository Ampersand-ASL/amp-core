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
#include <iostream>
#include <alsa/asoundlib.h>
#include <cmath> 
#include <queue>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/LinuxPollTimer.h"
#include "kc1fsz-tools/linux/StdClock.h"
#include "kc1fsz-tools/fixedqueue.h"

#include "ChannelIAX2.h"
#include "ChannelUsb.h"
#include "MessageBus.h"
#include "RegisterTask.h"
#include "ManagerTask.h"
#include "Synthesizer.h"
#include "Analyzer.h"

using namespace std;
using namespace kc1fsz;

//static const char *alsaDeviceName = "default"; // Or "hw:0,0", etc.
static const char* alsaDeviceName = "plughw:0,0"; // Or "hw:0,0", etc.
// This will be determined by looking at the installed cards.
// Check out /proc/asound
static const char* usbHidName = "/dev/hidraw0";

static const char* NODE_NUMBER_0 = "672730";
static const char* NODE_PASSWORD_0 = "xxxxx";
static int MANAGER_PORT_0 = 5038;

static const char* NODE_NUMBER_1 = "672731";
static const char* NODE_PASSWORD_1 = "xxxx";

//static const char* TARGET_NODE_NUMBER = "55553"; // Parrot (Works)
//static const char* TARGET_NODE_NUMBER = "50015";  // LUNCH 
//static const char* TARGET_NODE_NUMBER = "571570";  // David NR9V
static const char* TARGET_NODE_NUMBER = "27339";  // ECR (Works)

// Connects the manager to the IAX channel (TEMPORARY)
class ManagerSink : public ManagerTask::CommandSink {
public:

    ManagerSink(ChannelIAX2& ch) 
    :   _ch(ch) { }

    void execute(const char* cmd) { 
        _ch.processManagementCommand(cmd);
    }

private:

    ChannelIAX2& _ch;
};

int main(int argc, const char** argv) {

    Log log;
    log.info("Start");

    // Get libcurl going
    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res) {
        log.error("Libcurl failed");
        return -1;
    }

    StdClock clock;
    LinuxPollTimer timer1s(1000000);
    LinuxPollTimer timer20ms(20000);
    LinuxPollTimer timer10s(10000000);

    RegisterTask registerTask0(log, clock);
    registerTask0.configure("https://register.allstarlink.org", NODE_NUMBER_0, NODE_PASSWORD_0);

    RegisterTask registerTask1(log, clock);
    registerTask1.configure("https://register.allstarlink.org", NODE_NUMBER_1, NODE_PASSWORD_1);
  
    unsigned appRptId = 10;

    // Connects IAX2->analyzer
    MessageBus busIn;
    // Connects synth->IAX2
    MessageBus busOut;

    /*
    ChannelUsb radio0(log, clock, bus1, 1);
    int rc = radio0.open(alsaDeviceName, usbHidName);
    if (rc < 0) {
        log.error("%d", rc);
        return -1;
    }
        */

    ChannelIAX2 iax2Channel0(log, clock, 1, busIn, appRptId, 0);
    //iax2Channel0.setTrace(true);

    // The listening node
    iax2Channel0.open(4569, "radio");

    ManagerSink mgrSink0(iax2Channel0);
    ManagerTask mgrTask0(log, clock, MANAGER_PORT_0);
    mgrTask0.setCommandSink(&mgrSink0);

    Synthesizer syn0(log, clock, busOut, 0);
    Analyzer an0(log, clock, busOut, 0);

    // Routes
    busIn.targetChannel = &an0;
    busOut.targetChannel = &iax2Channel0;

    // Main loop        
    uint32_t lastOverflow = 0;
    long fastestLoopUs = 0, slowestLoopUs = 0;
    long totalLoopTimeUs = 0, loopCount = 0;
    timeval tv0;

    timer1s.reset();
    timer20ms.reset();

    Runnable* tasks[16] = { &iax2Channel0, &registerTask0, &registerTask1, &mgrTask0,
        &syn0, &an0 };
    unsigned taskCount = 6;

    int state = 0;
    uint32_t targetTime = clock.time() + 5000;

    while (true) {        

        gettimeofday(&tv0, NULL); 
        long loopStartUs = (long)tv0.tv_sec * 1000000 + tv0.tv_usec;

        // Gather the FDs that we care about
        const unsigned fdsCapacity = 32;
        unsigned fdsSize = 0;
        pollfd fds[fdsCapacity];

        for (unsigned i = 0; i < taskCount; i++) {
            int used = tasks[i]->populateFdSets(fds + fdsSize, fdsCapacity - fdsSize);
            if (used < 0) {
                log.error("Not enough poll fds");
                break;
            }
            fdsSize += used;
        }

        // Figure out how long we can sleep without missing the 
        // end of the current 20ms interval.
        uint32_t sleepMs = std::min(timer20ms.usLeftInInterval() / 1000L, 5UL);

        // Block waiting for activity or timeout
        int rc = poll(fds, fdsSize, sleepMs);
        if (rc < 0) {
            log.error("Poll error");
        } else {
            for (unsigned i = 0; i < taskCount; i++)
                tasks[i]->fastTick();
        }

        if (timer20ms.poll()) {
            for (unsigned i = 0; i < taskCount; i++)
                tasks[i]->audioRateTick();
        }  

        if (timer1s.poll()) {
            for (unsigned i = 0; i < taskCount; i++)
                tasks[i]->oneSecTick();
        }
        if (timer10s.poll()) {
            for (unsigned i = 0; i < taskCount; i++)
                tasks[i]->tenSecTick();
        }

        if (state == 0) {
            if (clock.isPast(targetTime)) {
                iax2Channel0.call(NODE_NUMBER_0, TARGET_NODE_NUMBER);
                iax2Channel0.call(NODE_NUMBER_1, TARGET_NODE_NUMBER);
                state = 1;
                targetTime = clock.time() + 5000;
            }
        }
        else if (state == 1) {
            if (clock.isPast(targetTime)) {        
                log.info("Sending tone");
                // Subaudible
                syn0.playTone(100, 100);
                state = 2;
            }
        }

        gettimeofday(&tv0, NULL); 
        long loopEndUs = (long)tv0.tv_sec * 1000000 + tv0.tv_usec;
        long loopTimeUs = loopEndUs - loopStartUs;
        totalLoopTimeUs += loopTimeUs;
        loopCount++;

        // Status
        if (fastestLoopUs == 0 || fastestLoopUs > loopTimeUs)
            fastestLoopUs = loopTimeUs;
        if (slowestLoopUs < loopTimeUs) {
            log.info("Slowest loop %ld us", slowestLoopUs);
            slowestLoopUs = loopTimeUs;
        }

        if (timer10s.poll()) {
            //log.info("Loop average %ld us", (totalLoopTimeUs / loopCount));
            totalLoopTimeUs = 0;
            loopCount = 0;
        }
    }

    iax2Channel0.close();

    log.info("Done");

    return 0;
}
