#include <iostream>
#include <unistd.h>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "EventLoop.h"
#include "StatsTask.h"

using namespace std;
using namespace kc1fsz;

/*
Development:
export AMP_NODE0_NUMBER=672731
export AMP_IAX_PROTO=IPV4
export AMP_ASL_STAT_URL=http://stats.allstarlink.org/uhandler
*/
int main(int,const char**) {

    Log log;

    // Get libcurl going
    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res) {
        log.error("Libcurl failed");
        return -1;
    }

    log.info("Start");
    StdClock clock;

    StatsTask task(log, clock, "1.0");
    task.configure(
        getenv("AMP_ASL_STAT_URL"), getenv("AMP_NODE0_NUMBER"));
  
    // Main loop        
    Runnable2* tasks2[] = { &task };
    EventLoop::run(log, clock, 0, 0, tasks2, std::size(tasks2), nullptr, false);

    curl_global_cleanup();

    log.info("End");
}
