#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "NullConsumer.h"
#include "SignalIn.h"

using namespace std;
using namespace kc1fsz;

int main(int argc, const char** argv) {

    Log log;
    log.info("Start");

    StdClock clock;
    NullConsumer bus;

    amp::SignalIn sin(log, clock, bus);

}

