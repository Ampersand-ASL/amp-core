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
#include <errno.h>

#ifndef _WIN32
#include <pthread.h>
#endif

#include "ThreadUtil.h"

namespace kc1fsz {
    namespace amp {

void setThreadName(const char* name) {
#ifndef _WIN32
    pthread_setname_np(pthread_self(), name);
#endif
}

void lowerThreadPriority() {
#ifndef _WIN32
    // All of this is used to reduce the priority of the service thread.
    const pthread_t self_thread = pthread_self();
    int policy;
    struct sched_param param;

    // Get current scheduling parameters
    if (pthread_getschedparam(self_thread, &policy, &param) != 0) {
        perror("pthread_getschedparam failed");
        return;
    }
    if (policy != SCHED_OTHER) {
        // Set the new policy to SCHED_OTHER and priority to 0 (required for SCHED_OTHER)
        param.sched_priority = 0;
        if (pthread_setschedparam(self_thread, SCHED_OTHER, &param) != 0) {
            perror("pthread_setschedparam to SCHED_OTHER failed");
            // Check for EPERM if it was previously a real-time policy and no capabilities are set
            if (errno == EPERM) {
                printf("Permission denied. Ensure the process has CAP_SYS_NICE if changing from a real-time policy.\n");
            }
            return;
        }
        /*
        // Get current scheduling parameters
        if (pthread_getschedparam(self_thread, &policy, &param) != 0) {
            perror("pthread_getschedparam failed");
            return;
        }
        printf("Thread policy: %d, priority: %d", policy, param.sched_priority);
        */
    }
#endif
}

    }
}
