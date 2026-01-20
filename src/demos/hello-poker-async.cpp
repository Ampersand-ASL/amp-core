#include <iostream>
#include <thread>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"
#include "kc1fsz-tools/threadsafequeue.h"

#include "EventLoop.h"
#include "Poker.h"
#include "Message.h"
#include "MessageConsumer.h"
#include "MultiRouter.h"

using namespace std;
using namespace kc1fsz;

class TestSink : public MessageConsumer {
public:
    void consume(const Message& msg) {
        cout << "Got" << endl;
        Poker::Result r;
        memcpy(&r,  msg.body(), sizeof(r));
        cout << "code " << r.code << endl;
        cout << "time " << r.pokeTimeMs << endl;
        cout << "addr " << r.addr4 << endl;
        cout << "port " << r.port << endl;
    }
};

threadsafequeue<Message> reqQueue;
threadsafequeue<Message> respQueue;

struct WorkerThreadArgs {
    Log* log;
    Clock* clock;
};

void worker_thread(const WorkerThreadArgs* args) {

    Poker::Result r = Poker::poke(*(args->log), *(args->clock), "61057", 250);
    Message res(Message::Type::NET_DIAG_1_REQ, 0, 
        sizeof(Poker::Result),  (const uint8_t*)&r, 0, 0);
    res.setSource(0, 0);
    res.setDest(1, 1);
    respQueue.push(res);
}

int main(int, const char**) {

    Log log;
    StdClock clock;
    MultiRouter bus(respQueue);
    TestSink sink1;
    bus.addRoute(&sink1, 1);

    WorkerThreadArgs args;
    args.log = &log;
    args.clock = &clock;
    std::thread t0(worker_thread, &args);

    /*
    {
        Poker::Result r = Poker::poke(log, clock, "61057", 250);
        cout << "code " << r.code << endl;
        cout << "time " << r.pokeTimeMs << endl;
        cout << "addr " << r.addr4 << endl;
        cout << "port " << r.port << endl;
    }
    */

    Runnable2* tasks[] = { &bus };
    EventLoop::run(log, clock, 0, 0, tasks, std::size(tasks));
}
