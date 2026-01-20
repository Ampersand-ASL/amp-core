#include <iostream>
#include <thread>
#include <thread>
#include <chrono>
#include <cstring>

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

int main(int, const char**) {

    threadsafequeue<Message> reqQueue;
    threadsafequeue<Message> respQueue;

    Log log;
    StdClock clock;
    MultiRouter bus(respQueue);
    TestSink sink1;
    bus.addRoute(&sink1, 1);
    std::atomic<bool> runFlag(true);

    std::thread t0(Poker::loop, &log, &clock, &reqQueue, &respQueue, &runFlag);

    Poker::Request req = { .bindAddr = "0.0.0.0", .nodeNumber = "61057", .timeoutMs = 250};
    Message msg(Message::Type::NET_DIAG_1_REQ, 0, 
        sizeof(Poker::Request),  (const uint8_t*)&req, 0, 0);
    reqQueue.push(msg);

    Runnable2* tasks[] = { &bus };
    EventLoop::run(log, clock, 0, 0, tasks, std::size(tasks));
}
