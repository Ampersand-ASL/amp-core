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
#include <filesystem> 
#include <fstream>

#include "kc1fsz-tools/Common.h"
#include "ProgramUtils.h"

using namespace std;
namespace fs = std::filesystem;

namespace kc1fsz {
    namespace amp {
        namespace ProgramUtils {

/**
 * @returns A vector of the filenames (full path) of the segments.
 */
queue<string> getSegments(const char* programRoot) {
    queue<string> result;
    // Probe for files until we can't find one
    for (unsigned i = 0; i < 64; i++) {
        char fn[128];
        snprintf(fn, sizeof(fn), "%s/segments/seg%u.sln", programRoot, i);
        if (fs::exists(fn)) {
            result.push(string(fn));
            continue;
        }
        snprintf(fn, sizeof(fn), "%s/segments/seg%u.s16", programRoot, i);
        if (fs::exists(fn)) {
            result.push(string(fn));
            continue;
        }
        // If we get to this point then the file wasn't found, that's the end
        break;
    }
    return result;
}

queue<string> getBreaks(const char* programRoot) {
    queue<string> result;
    // Probe for files until we can't find one
    for (unsigned i = 0; i < 64; i++) {
        char fn[128];
        snprintf(fn, sizeof(fn), "%s/announcements/break%u.txt", programRoot, i);
        if (fs::exists(fn)) {
            result.push(string(fn));
            continue;
        }
        // If we get to this point then the file wasn't found, that's the end
        break;
    }
    return result;
}

static string loadText(const string& fn) {
    string result;
    ifstream f(fn);

    if (f.is_open()) {
        string line;
        while (std::getline(f, line)) {
            trim(line);
            if (line.empty())
                continue;
            if (!result.empty())
                result += " ";
            result += line;
        }
    }
    return result;
}

int loadProgramStandard(const char* programRoot,
    unsigned initialGapMs, unsigned gapMs, std::vector<BridgeCall::ProgramStep>& steps) {
    
    // Sanity check on directory structure
    if (!fs::exists(string(programRoot) + "/segments"))
        return -1;
    if (!fs::exists(string(programRoot) + "/announcements"))
        return -2;

    string text;

    // Intro and initial pause
    text = loadText(string(programRoot) + "/announcements/intro.txt");
    if (!text.empty()) {
        steps.push_back({.type = BridgeCall::ProgramStep::StepType::TTS, 
            .arg0 = text });
        steps.push_back({.type = BridgeCall::ProgramStep::StepType::PAUSE, 
            .arg0 = "", .intervalMs = initialGapMs });
    }

    queue<string> breakFiles;

    // Segments
    queue<string> segFiles = getSegments(programRoot);
    while (!segFiles.empty()) {

        string sf = segFiles.front();
        segFiles.pop();

        // Play the segment file
        steps.push_back({.type = BridgeCall::ProgramStep::StepType::FILE, 
            .arg0 = sf });

        // Inter-segment break (if there is anything left)
        if (!segFiles.empty()) {
            // Replenish breaks if needed
            if (breakFiles.empty())
                breakFiles = getBreaks(programRoot);
            // Add the break
            if (!breakFiles.empty()) {
                text = loadText(breakFiles.front());
                breakFiles.pop();
                if (!text.empty())
                    steps.push_back({.type = BridgeCall::ProgramStep::StepType::TTS, 
                        .arg0 = text });
            }
            // Inter-segment pause
            steps.push_back({.type = BridgeCall::ProgramStep::StepType::PAUSE, 
                .arg0 = "", .intervalMs = gapMs });
        }
    }

    // Outro
    text = loadText(string(programRoot) + "/announcements/outro.txt");
    if (!text.empty())
        steps.push_back({.type = BridgeCall::ProgramStep::StepType::TTS, 
            .arg0 = text });

    return 0;
}

}
    }
}
