#include <iostream>
#include <cassert>

#include "IAX2Util.h"

using namespace std;
using namespace kc1fsz;

int main(int,const char**) {

    assert(codecLetterToMask('D') == CODECType::IAX2_CODEC_G711_ULAW);
    assert(codecMaskToLetter(CODECType::IAX2_CODEC_G711_ULAW) == 'D');

    cout << codecMaskToLetter(CODECType::IAX2_CODEC_SLIN_16K) << endl;
    cout << codecMaskToLetter(CODECType::IAX2_CODEC_SLIN_8K) << endl;
    cout << codecMaskToLetter(CODECType::IAX2_CODEC_G711_ULAW) << endl;

    uint32_t result[3];
    assert(parseCodecPref("EDH", result, 3) == 3);
    assert(result[0] == CODECType::IAX2_CODEC_G711_ALAW);
    assert(result[1] == CODECType::IAX2_CODEC_G711_ULAW);
    assert(result[2] == CODECType::IAX2_CODEC_SLIN_8K);

    {
        // Preference tests.
        // In this test the only place the caller and the callee share a capability
        // is with the G711_ULAW CODEC.
        uint32_t callerPrefs[2];
        callerPrefs[0] = CODECType::IAX2_CODEC_SLIN_8K;
        callerPrefs[1] = CODECType::IAX2_CODEC_G711_ULAW;
        uint32_t callerCapability = CODECType::IAX2_CODEC_SLIN_8K | 
            CODECType::IAX2_CODEC_G711_ULAW;

        uint32_t calleePrefs[1];
        calleePrefs[0] = CODECType::IAX2_CODEC_SLIN_16K;
        uint32_t calleeCapability = CODECType::IAX2_CODEC_G711_ULAW | 
            CODECType::IAX2_CODEC_SLIN_16K;

        assert(assignCodec(callerCapability, 0, callerPrefs, 2,
            calleeCapability, calleePrefs, 1) == CODECType::IAX2_CODEC_G711_ULAW);
    }
}
