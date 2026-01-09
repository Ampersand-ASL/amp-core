#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"
#include "TraceLog.h"

using namespace std;
using namespace kc1fsz;

int main(int,const char**) {

    const unsigned dataLen = 4;
    std::string data[dataLen];
    StdClock clock;
    TraceLog log(clock, data, dataLen);
    log.info("Test 1");
    log.info("Test 2");
    log.info("Test 3");
    cout << "Test 1" << endl;
    log.visitAll(
        [](const string& msg) {
            cout << msg << endl;
        }
    );
    log.info("Test 4");
    log.info("Test 5");
    log.info("Test 6");
    cout << "Test 2" << endl;
    log.visitAll(
        [](const string& msg) {
            cout << msg << endl;
        }
    );
}
