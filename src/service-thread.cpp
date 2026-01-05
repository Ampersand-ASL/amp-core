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
#ifndef _WIN32
// This stuff is needed for the thread priority manipulation
#include <sched.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h> 
#include <alsa/asoundlib.h>
#include <execinfo.h>
#include <signal.h>
#endif

#include <iostream>
#include <string>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "RegisterTask.h"
#include "StatsTask.h"
#include "EventLoop.h"
#include "ConfigPoller.h"
#include "ThreadUtil.h"

#include "service-thread.h"

using namespace std;
using namespace kc1fsz;

void service_thread(void* ud) {

    amp::setThreadName("SVC");

    // Pull out the thread startup arguments
    const service_thread_args* args = (service_thread_args*)ud;
    const string cfgFileName = args->cfgFileName;
    Log& log = *(args->log);

    log.info("service_thread start");

    // Sleep waiting to change real-time status
    sleep(10);
    amp::lowerThreadPriority();

    StdClock clock;

    RegisterTask registerTask(log, clock);

    StatsTask statsTask(log, clock, "1.0.0");

    amp::ConfigPoller cfgPoller(log, cfgFileName.c_str(), 
        // This function will be called on any update to the configuration document.
        [&log, &registerTask, &statsTask](const json& cfg) {
            try {
                registerTask.configure(
                    cfg["aslRegUrl"].get<std::string>().c_str(), 
                    cfg["node"].get<std::string>().c_str(), 
                    cfg["password"].get<std::string>().c_str(), 
                    std::stoi(cfg["iaxPort"].get<std::string>()));

                statsTask.configure(
                    cfg["aslStatUrl"].get<std::string>().c_str(), 
                    cfg["node"].get<std::string>().c_str());
            }
            catch (...) {
                log.error("Failed to load configuration");
            }
        }
    );

    // Main loop        
    Runnable2* tasks2[] = { &registerTask, &cfgPoller };
    EventLoop::run(log, clock, 0, 0, tasks2, std::size(tasks2));

    // #### TODO: NEED A CLEAN WAY TO EXIT THIS THREAD
}
