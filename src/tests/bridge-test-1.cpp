#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "Bridge.h"

using namespace std;
using namespace kc1fsz;

int main(int, const char**) {
    
    Log log;
    StdClock clock;
    amp::Bridge bridge(log, clock);

    cout << "hello izzy" << endl;
}
