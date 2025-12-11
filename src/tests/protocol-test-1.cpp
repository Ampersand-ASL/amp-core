
#include <iostream>
#include <alsa/asoundlib.h>
#include <cmath> 
#include <queue>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/NetUtils.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "LineIAX2.h"
#include "NullConsumer.h"
#include "RegisterTask.h"
#include "EventLoop.h"

using namespace std;
using namespace kc1fsz;

static const char* NODE_NUMBER = "61057";
static const char* NODE_PASSWORD = "microlink";
static const int IAX_PORT = 4569;
// ECR
//static const char* TARGET_NODE_NUMBER = "27339";
// TX PARROT
//static const char* TARGET_NODE_NUMBER = "55553";
// ASL PARROT
static const char* TARGET_NODE_NUMBER = "61057";

class LocalRegistryStd : public LocalRegistry {
public:
    virtual bool lookup(const char* destNumber, sockaddr_storage& addr) {
        /*
        if (strcmp(destNumber, "61057") == 0) {
            addr.ss_family = AF_INET6;
            setIPAddr(addr, "2600:1f1c:7c8:c7d6:83ed:9d7d:d2b8:f430");
            setIPPort(addr, 4569);
            char temp[64];
            formatIPAddrAndPort((const sockaddr&)addr, temp, 64);
            return true;
        } else {
            return false;
        }
            */
        return false;
    }
};

/*
export AMP_IAX_PROTO=IPV6
export AMP_IAX_PORT=4569
*/
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

    NullConsumer bus;

    RegisterTask registerTask(log, clock);
    registerTask.configure("https://register.allstarlink.org", NODE_NUMBER, NODE_PASSWORD, IAX_PORT);

    LocalRegistryStd locReg;
    LineIAX2 iax0(log, clock, 1, bus, 0, &locReg);

    // Determine the address family, defaulting to IPv4
    short addrFamily = getenv("AMP_IAX_PROTO") != 0 && 
        strcmp(getenv("AMP_IAX_PROTO"), "IPV6") == 0 ? AF_INET6 : AF_INET;
    iax0.open(addrFamily, atoi(getenv("AMP_IAX_PORT")), "radio");
    iax0.setTrace(true);

    bool called = false;
    uint32_t startTime = clock.time();

    // Main loop        
    Runnable* tasks[16] = { &registerTask, &iax0 };
    unsigned taskCount = 2;
    EventLoop::run(log, clock, tasks, taskCount,
        [&called, startTime, &iax0](Log& log, Clock& clock) {
            // Decide whether to initiate a call
            if (!called && clock.time() - startTime > 2 * 1000) {
                called = true;
                iax0.call(NODE_NUMBER, TARGET_NODE_NUMBER);
            }
            return true;
        });

    iax0.close();
    log.info("Done");

    return 0;
}
