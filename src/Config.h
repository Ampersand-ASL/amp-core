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
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace kc1fsz {
    namespace amp {

class Config {
public:

    Config() { }

    Config(const Config& other) {
        *this = other;
    }

    Config& operator=(const Config& other) {
        lastUpdateMs = other.lastUpdateMs;
        call = other.call;
        node = other.node;
        password = other.password;
        audioDeviceType = other.audioDeviceType;
        audioDeviceUsbBus = other.audioDeviceUsbBus;
        audioDeviceUsbPort = other.audioDeviceUsbPort;
        iaxPort4 = other.iaxPort4;
        aslRegUrl = other.aslRegUrl;
        aslStatUrl = other.aslStatUrl;
        aslDnsRoot = other.aslDnsRoot;
        privateKey = other.privateKey;
        return *this;
    }

    uint64_t lastUpdateMs = 0;
    std::string call;
    std::string node;
    std::string password;
    std::string audioDeviceType; 
    std::string audioDeviceUsbBus;
    std::string audioDeviceUsbPort;
    std::string iaxPort4;
    std::string aslRegUrl;
    std::string aslStatUrl;
    std::string aslDnsRoot;
    std::string privateKey;
    
    void setDefaults();

    nlohmann::json toJson();
    void fromJson(nlohmann::json j);
};

    }
}
