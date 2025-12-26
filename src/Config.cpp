/**
 * Copyright (C) 2025, Bruce MacKinnon KC1FSZ
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "Config.h"

using namespace std;
using json = nlohmann::json;

namespace kc1fsz {
    namespace amp {

void Config::setDefaults() {
    iaxPort4 = "4569";
    aslRegUrl = "https://register.allstarlink.org";
    aslStatUrl = "=http://stats.allstarlink.org/uhandler";
    aslDnsRoot = "nodes.allstarlink.org";
}

json Config::toJson() {
    json o;
    o["lastUpdateMs"] = lastUpdateMs;
    o["call"] = call;
    o["node"] = node;
    o["password"] = password;
    o["audioDeviceType"] = audioDeviceType;
    o["audioDeviceUsbBus"] = audioDeviceUsbBus;
    o["audioDeviceUsbPort"] = audioDeviceUsbPort;
    o["iaxPort4"] = iaxPort4;
    o["aslRegUrl"] = aslRegUrl;
    o["aslStatUrl"] = aslStatUrl;
    o["aslDnsRoot"] = aslDnsRoot;
    o["privateKey"] = privateKey;
    return o;
}

void Config::fromJson(json o) {
    lastUpdateMs = o["lastUpdateMs"].get<uint64_t>();
    call = o["call"].get<std::string>();
    node = o["node"].get<std::string>();
    password = o["password"].get<std::string>();
    audioDeviceType = o["audioDeviceType"].get<std::string>();
    audioDeviceUsbBus = o["audioDeviceUsbBus"].get<int>();
    audioDeviceUsbPort = o["audioDeviceUsbPort"].get<int>();
    iaxPort4 = o["iaxPort4"].get<int>();
    aslRegUrl = o["aslRegUrl"].get<std::string>();
    aslStatUrl = o["aslStatUrl"].get<std::string>();
    aslDnsRoot = o["aslDnsRoot"].get<std::string>();
    privateKey = o["privateKey"].get<std::string>();
}

    }
}
