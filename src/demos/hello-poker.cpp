#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"

#include "Poker.h"

using namespace std;
using namespace kc1fsz;

int main(int, const char**) {

    Log log;
    StdClock clock;

    {
        Poker::Result r = Poker::poke(log, clock, "0.0.0.0", "61057", 250);
        cout << "code " << r.code << endl;
        cout << "time " << r.pokeTimeMs << endl;
        cout << "addr " << r.addr4 << endl;
        cout << "port " << r.port << endl;
    }
    {
        Poker::Result r = Poker::poke(log, clock, "0.0.0.0", "55553", 250);
        cout << "code " << r.code << endl;
        cout << "time " << r.pokeTimeMs << endl;
        cout << "addr " << r.addr4 << endl;
        cout << "port " << r.port << endl;
    }
    // Demonstrate a timeout
    {
        Poker::Result r = Poker::poke(log, clock, "0.0.0.0", "61057", 5);
        cout << "code " << r.code << endl;
        cout << "time " << r.pokeTimeMs << endl;
        cout << "addr " << r.addr4 << endl;
        cout << "port " << r.port << endl;
    }
    // Demonstrate an unknown node
    {
        Poker::Result r = Poker::poke(log, clock, "0.0.0.0", "6105799", 5);
        cout << "code " << r.code << endl;
        cout << "time " << r.pokeTimeMs << endl;
        cout << "addr " << r.addr4 << endl;
        cout << "port " << r.port << endl;
    }
    // Demonstrate a bad bind
    {
        Poker::Result r = Poker::poke(log, clock, "1.0.0.0", "61057", 5);
        cout << "code " << r.code << endl;
        cout << "time " << r.pokeTimeMs << endl;
        cout << "addr " << r.addr4 << endl;
        cout << "port " << r.port << endl;
    }

}
