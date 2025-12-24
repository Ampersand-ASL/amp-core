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

#include <nlohmann/json.hpp>

#include "kc1fsz-tools/fixedstring.h"
#include "kc1fsz-tools/fixedstring2.h"

namespace kc1fsz {
    namespace amp {

class Config {
public:

    fixedstring call;
    fixedstring node;
    fixedstring password;
    fixedstring audioDevice;
    int iaxPort4;
    fixedstring aslRegUrl;
    fixedstring aslStatUrl;
    fixedstring aslDnsRoot;
    fixedstring2<128> privateKey;
    
    void setDefaults();

    nlohmann::json toJson();
    void fromJson(nlohmann::json j);
};

    }
}
