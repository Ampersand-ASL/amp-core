#include <iostream>

#include <nlohmann/json.hpp>

#include "Config.h"

using namespace std;
using namespace kc1fsz;

using json = nlohmann::json;

int main(int, const char**) {
    
    amp::Config cfg;
    cfg.setDefaults();
    cfg.call = "KC1FSZ";
    cout << cfg.toJson().dump() << endl;

    amp::Config cfg2;
    cfg2.fromJson(cfg.toJson());
    cout << cfg2.toJson().dump() << endl;
}
