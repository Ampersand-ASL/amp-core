#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "tests/TestConsumer.h"
#include "SignalIn.h"
#include "EventLoop.h"

using namespace std;
using namespace kc1fsz;

int main(int argc, const char** argv) {

    Log log;
    log.info("Start");

    StdClock clock;
    TestConsumer bus;

    amp::SignalIn sin(log, clock, bus, 2, 
        Message::SignalType::COS_ON, Message::SignalType::COS_OFF);
    sin.openHid("/dev/hidraw0");

    // Main loop        
    Runnable2* tasks2[] = { &sin };
    EventLoop::run(log, clock, 0, 0, tasks2, std::size(tasks2));
}

