#include <iostream>
#include <cassert>
#include <cstring>

#include "ed25519.h"

#include "kc1fsz-tools/Common.h"

/*
This is a demonstration of the use of this ED25519 library:
https://github.com/orlp/ed25519
*/

// URL for good web demo: https://cyphr.me/ed25519_tool/ed.html#?alg_type=Msg&msg_enc=Text&msg=Test&key_enc=Hex&seed=95EBE5A75615C97A3272163CC842EAF8EC0F79C3215189A24403ADF3A47E0DEC&key=7FDD773A09DD6024A15930EF2ED16F39DC62EECC06B4F68FA41AB9B22CA6BC69&sig=922621D14D45456C22C8ADE15FAE77D28E72CFB08565ADE548BDD89913E05042E9AD5CF22B7896FA8F323A81EF4ADF8BCF3661B2B5FCA9CB95E5806FE8C5BA08&verify
const char* seed_hex = "95EBE5A75615C97A3272163CC842EAF8EC0F79C3215189A24403ADF3A47E0DEC";
const char* msg      = "Test";

using namespace std;
using namespace kc1fsz;

int main(int, const char**) {

    unsigned char seed[32];
    asciiHexToBin(seed_hex, 64, seed, 32);

    unsigned char pub[32];
    unsigned char priv[64];
    ed25519_create_keypair(pub, priv, seed);

    unsigned char sig[64];
    ed25519_sign(sig, (const uint8_t*)msg, 4, pub, priv);

    char result[129];
    binToAsciiHex(sig, 64, result, 128);
    result[128] = 0;
    cout << "Signature " << result << endl;

    // End-to-end demo:

    // 1. Challenger creates a nonce message and remembers it
    int nonce = 123456;
    char nonce_txt[32];
    snprintf(nonce_txt, 32, "%d", nonce);
    
    // 2. Send the as a challenge

    // 3. The challenge recipient signs the nonce with its private key
    ed25519_sign(sig, (const uint8_t*)nonce_txt, strlen(nonce_txt), pub, priv);

    // 4. The challenge recipient sends the signature back to the challenger
    // as ASCII hex.
    char sig_hex[129];
    binToAsciiHex(sig, 64, sig_hex, 128);
    result[128] = 0;

    // 5. The challenger converts back to binary
    asciiHexToBin(sig_hex, 128, sig, 64);

    // 5. The challenger validates the signature using public key
    assert(ed25519_verify(sig, (const uint8_t*)nonce_txt, strlen(nonce_txt), pub) == 1);

    return 0;
}

