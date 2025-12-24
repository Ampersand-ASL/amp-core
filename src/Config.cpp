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
    audioDevice = "vendorname:\"C-Media  Electronics, Inc.\"";
    iaxPort4 = 4569;
    aslRegUrl = "https://register.allstarlink.org";
    aslStatUrl = "=http://stats.allstarlink.org/uhandler";
    aslDnsRoot = "nodes.allstarlink.org";
}

json Config::toJson() {
    json o;
    o["call"] = call.c_str();
    o["node"] = node.c_str();
    o["password"] = password.c_str();
    o["audioDevice"] = audioDevice.c_str();
    o["iaxPort4"] = iaxPort4;
    o["aslRegUrl"] = aslRegUrl.c_str();
    o["aslStatUrl"] = aslStatUrl.c_str();
    o["aslDnsRoot"] = aslDnsRoot.c_str();
    o["privateKey"] = privateKey.c_str();
    return o;
}

void Config::fromJson(json o) {
    call = o["call"].get<std::string>().c_str();
    node = o["node"].get<std::string>().c_str();
    password = o["password"].get<std::string>().c_str();
    audioDevice = o["audioDevice"].get<std::string>().c_str();
    iaxPort4 = o["iaxPort4"].get<int>();
    aslRegUrl = o["aslRegUrl"].get<std::string>().c_str();
    aslStatUrl = o["aslStatUrl"].get<std::string>().c_str();
    aslDnsRoot = o["aslDnsRoot"].get<std::string>().c_str();
    privateKey = o["privateKey"].get<std::string>().c_str();
}

    }
}
