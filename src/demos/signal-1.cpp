/*
A simple test that (a) displays COS status (b) keys and unkeys PTT on a timer.
*/
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "tests/TestConsumer.h"
#include "SimpleRouter.h"
#include "EventLoop.h"
#include "SignalIn.h"
#include "SignalOut.h"
#include "TimerTask.h"

using namespace std;
using namespace kc1fsz;

int main(int argc, const char** argv) {

    Log log;
    log.info("Start");

    StdClock clock;
    //TestConsumer bus;
    SimpleRouter bus;

    amp::SignalIn sin(log, clock, bus, 2, 
        Message::SignalType::COS_ON, Message::SignalType::COS_OFF);
    sin.openHid("/dev/hidraw0");

    amp::SignalOut sout(log, clock, bus, 
        Message::SignalType::PTT_ON, Message::SignalType::PTT_OFF);
    bus.addRoute(&sout, 31);
    int rc = sout.openHid("/dev/hidraw0");
    if (rc != 0) {
        log.info("Failed to open for output %d", rc);
    }

    bool ptt = false;

    TimerTask task1(log, clock, 3, [&ptt, &bus]() {
        MessageEmpty msg;
        if (!ptt) {
            msg  = MessageEmpty::signal(Message::SignalType::PTT_ON);
        } else {
            msg  = MessageEmpty::signal(Message::SignalType::PTT_OFF);
        }
        msg.setDest(31, Message::UNKNOWN_CALL_ID);
        ptt = !ptt;
        bus.consume(msg);
    });

    // Main loop        
    log.info("In loop");
    Runnable2* tasks2[] = { &sin, &sout, &task1 };
    EventLoop::run(log, clock, 0, 0, tasks2, std::size(tasks2));
}

