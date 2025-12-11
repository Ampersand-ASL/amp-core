#include <iostream>
#include <unistd.h>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/LinuxPollTimer.h"
#include "kc1fsz-tools/linux/StdClock.h"

using namespace std;
using namespace kc1fsz;

#include "RegisterTask.h"

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

    RegisterTask task(log, clock);
    task.configure("https://register.allstarlink.org", "61057", "xxxxxx");
  
    while (true) {
        task.fastTick();
    }

    curl_global_cleanup();

    log.info("End");
}
