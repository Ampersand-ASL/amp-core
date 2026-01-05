#include "kc1fsz-tools/Log.h"

#include "NRLog.h"

using namespace std;
using namespace kc1fsz;

// Get this from the New Relic account setup
static const char* NR_API_KEY = "xxx";

int main(int argc, const char** argv) {
    NRLog log("Ampersand Parrot", "asl-prod", NR_API_KEY);
    log.info("Hello Izzy 12");
    log.info("Hello Izzy 13");
    log.stop();
}




