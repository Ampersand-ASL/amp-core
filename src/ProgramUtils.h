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

#include <vector>
#include <queue>

#include "BridgeCall.h"

namespace kc1fsz {
    namespace amp {
        namespace ProgramUtils {

/**
 * @returns A vector of the filenames (full path) of the segments.
 */
std::queue<std::string> getSegments(const char* programRoot);

/**
 * @returns A vector of the filenames (full path) of the segments.
 */
std::queue<std::string> getBreaks(const char* programRoot);

/**
 * Creates a standard program using the files located at the program root.
 */
int loadProgramStandard(const char* programRoot,
    unsigned initialGapMs, unsigned gapMs, std::vector<BridgeCall::ProgramStep>& steps);

        }
    }
}
