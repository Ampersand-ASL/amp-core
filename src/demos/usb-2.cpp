/*
 */
#include <iostream>
#include <cassert>

#include <alsa/asoundlib.h>

#include "sound-map.h"
#include "LineUsb.h"

using namespace std;
using namespace kc1fsz;

int main(int, const char**) {
    
    const char* targetVendorName = "C-Media Electronics, Inc.";
    char query[64];
    snprintf(query, 64, "vendorname:\"%s\"", targetVendorName);
    int alsaDev;
    string ossDev;
    int rc = querySoundMap(query, alsaDev, ossDev);
    if (rc < 0) {
        cout << "querySoundMap() ERROR: " << rc << endl;
        return -1;
    }

    cout << "Found the device:" << endl;
    cout << " ALSA  : " << alsaDev << endl;
    cout << " OSS   : " << ossDev << endl;

    // Get range
    char name[32];
    snprintf(name, 32, "hw:%d", alsaDev);

    int minV, maxV;
    int rc2 = getMixerRange(name, "Speaker Playback Volume", &minV, &maxV);
    cout << rc2 << endl;
    assert(rc2 == 0);

    cout << "Range " << minV << " " << maxV << endl;

    // Show in dB
    float minDb, maxDb;
    getConvertMixerValueToDb(name, "Speaker Playback Volume", minV, &minDb);
    getConvertMixerValueToDb(name, "Speaker Playback Volume", maxV, &maxDb);
    cout << "Range dB " << minDb << " " << maxDb << endl;

    return 0;
}
