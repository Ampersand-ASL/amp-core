#include <iostream>
#include <cstring>
#include <cassert>
#include <sys/time.h>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/LinuxPollTimer.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "kc1fsz-tools/fixedstring.h"
#include "kc1fsz-tools/fixedqueue.h"

#include "ManagerTask.h"

using namespace std;
using namespace kc1fsz;

static void unit_test_1() {
    ManagerTask::Session s;
    s.reset();
    const char t0[] = "abc\r\n\r\ndef\r\n\r\n";
    memcpy(s.inBuf, t0, strlen(t0));
    s.inBufLen = strlen(t0);
    char c[65];
    assert(s.popCommandIfPossible(c, 65));
    assert(strcmp(c,"abc") == 0);
    // Purposely constrained
    assert(s.popCommandIfPossible(c, 3));
    assert(strcmp(c,"de") == 0);
    assert(!s.popCommandIfPossible(c, 65));

    const char t1[] = "abc\r\n";
    memcpy(s.inBuf, t1, strlen(t1));
    s.inBufLen = strlen(t1);
    assert(!s.popCommandIfPossible(c, 65));
}

static void unit_test_2() {

    const char* t0 = "a: b\r\nc: d\r\n";
    int i = 0;
    int rc = visitValues(t0, strlen(t0), [&i](const char* n, const char* v) { 
        i++;
    });
    assert(rc == 0 && i == 2);

    const char* t1 = "a: b\r\nc: d";
    fixedstring c0;
    fixedstring c1;
    i = 0;
    rc = visitValues(t1, strlen(t1), [&i, &c0, &c1](const char* n, const char* v) { 
        c0 = n;
        c1 = v;
        i++;
    });
    assert(rc == 0 && i == 2 && c0 == "c" && c1 == "d");

    const char* t2 = "a0123456789012345678901234567890123456789012345678901234567890123456789: b\r\nc: d";
    i = 0;
    rc = visitValues(t2, strlen(t2), [&i, &c0, &c1](const char* n, const char* v) { 
        i++;
    });
    assert(rc == -1);
}

class DemoSink : public ManagerTask::CommandSink {
public:
    void execute(const char* cmd) {
        cout << "GOT A COMMAND TO EXECUTE: " << cmd << endl;
        fixedstring tokensStore[8];
        fixedqueue<fixedstring> tokens(tokensStore, 8);
        if (tokenize(cmd, ' ', tokens) == 0) {
            for (unsigned i = 0; i < tokens.size(); i++) 
                cout << "Token " << i << " " << tokens.at(i).c_str() << endl;
        }
    }
};

int main(int argc, const char** argv) {

    unit_test_1();
    unit_test_2();

    Log log;
    log.info("Start");

    StdClock clock;
    LinuxPollTimer timer2ms(20000);

    ManagerTask manager(log, clock, 5038);
    DemoSink sink;
    manager.setCommandSink(&sink);
    
    // Main loop    
    timer2ms.reset();
    
    timeval tv0;
    long slowestUs = 0;

    while (true) {        

        gettimeofday(&tv0, NULL); 
        long loopStartUs = (long)tv0.tv_sec * 1000000 + tv0.tv_usec;

        manager.fastTick();

        gettimeofday(&tv0, NULL); 
        long loopEndUs = (long)tv0.tv_sec * 1000000 + tv0.tv_usec;
        long loopTimeUs = loopEndUs - loopStartUs;

        if (loopTimeUs > slowestUs) {
            log.info("Slowest %ld", loopTimeUs);
            slowestUs = loopTimeUs;
        }
    }

    log.info("Done");

    return 0;
}
