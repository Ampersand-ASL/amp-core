#include "kc1fsz-tools/Log.h"

#include "NRLog.h"

using namespace std;
using namespace kc1fsz;

// Get this from the New Relic account setup
static const char* NR_API_KEY = "aa7da00bbadf55b8e1d53d95001fef75FFFFNRAL";

int main(int argc, const char** argv) {
    NRLog log("Ampersand Parrot", "asl-prod", NR_API_KEY);
    log.info("Hello Izzy 5");
    log.info("Hello Izzy 6");
    log.stop();
}




