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
#include "kc1fsz-tools/Common.h"

#include "WebUi.h"

using namespace std;

namespace kc1fsz {
    namespace amp {

vector<pair<string, string>> WebUi::parseFavorites(const string& fav) {

    vector<pair<string,string>> result;

    // This is a comma-delimited list, but quoting can be used to provide
    // a token that contains a comma.
    string acc0;
    string acc1;
    bool inQuote = false;
    int state = 0;
    for (unsigned i = 0; i < fav.length(); i++) {
        char c = fav.at(i);
        if (inQuote) {
            if (c == '"') 
                inQuote = false;
            else {
                if (state == 0)
                    acc0 += c;
                else 
                    acc1 += c;
            }
        }
        else {
            if (c == '"') 
                inQuote = true;
            else if (c == ':')
                state = 1;
            else if (c == ',') {
                trim(acc0);
                trim(acc1);
                if (!acc0.empty())
                    result.push_back(make_pair(acc0, acc1));
                acc0.clear();
                acc1.clear();
                state = 0;
            }
            else {
                if (state == 0)
                    acc0 += c;
                else 
                    acc1 += c;
            }
        }
    }
    // Clean up last token
    trim(acc0);
    trim(acc1);
    if (!acc0.empty())
        result.push_back(make_pair(acc0, acc1));
    return result;
}

    }
}
