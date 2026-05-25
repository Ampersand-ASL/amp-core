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
#include "ed25519.h"
#include "LineIAX2.h"

namespace kc1fsz {

bool LineIAX2::_isValidEd25519Signature(const uint8_t* sigBin, const char* challengeTxt,
    const uint8_t* publicKeyBin) {
    return ed25519_verify(sigBin, (const uint8_t*)challengeTxt, strlen(challengeTxt), 
        publicKeyBin) == 1;
}

void LineIAX2::_signEd25519(uint8_t* sig, const char* token, const char* privateKeyHex) {
    uint8_t seedBin[32];
    asciiHexToBin(privateKeyHex, 64, seedBin, 32);
    unsigned char pubBin[32];
    unsigned char privBin[64];
    ed25519_create_keypair(pubBin, privBin, seedBin);
    ed25519_sign(sig, (const uint8_t*)token, strlen(token), pubBin, privBin);
}

}
