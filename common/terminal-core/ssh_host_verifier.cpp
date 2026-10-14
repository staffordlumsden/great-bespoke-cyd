#include "ssh_host_verifier.h"
#ifdef ESP_PLATFORM
#include "mbedtls/sha256.h"
#else
// macOS test backend; firmware uses the pinned ESP-IDF mbedTLS implementation.
#include <CommonCrypto/CommonDigest.h>
#endif
namespace cm {
HostTrustResult verify_server_key(const uint8_t* raw_key,size_t size,
                                 const uint8_t expected[32],bool configured) {
    if(!configured || !expected)return HostTrustResult::unconfigured;
    // Bound work even when an untrusted peer provides malformed data.
    if(!raw_key || !size || size>16384)return HostTrustResult::invalid_key;
    uint8_t actual[32]{};
#ifdef ESP_PLATFORM
    if(mbedtls_sha256(raw_key,size,actual,0)!=0)return HostTrustResult::hash_error;
#else
    if(!CC_SHA256(raw_key,static_cast<CC_LONG>(size),actual))return HostTrustResult::hash_error;
#endif
    // Examine every digest byte. Volatile prevents substitution with an
    // early-return comparison; input size validation is not secret dependent.
    volatile unsigned difference=0;
    for(unsigned i=0;i<32;++i)difference=difference | (actual[i]^expected[i]);
    return difference?HostTrustResult::mismatch:HostTrustResult::verified;
}
const char* host_trust_name(HostTrustResult result) {
    switch(result) {
        case HostTrustResult::verified:return "VERIFIED";
        case HostTrustResult::unconfigured:return "TRUST MISSING";
        case HostTrustResult::invalid_key:return "INVALID HOST KEY";
        case HostTrustResult::hash_error:return "HOST KEY HASH ERROR";
        case HostTrustResult::mismatch:return "HOST KEY CHANGED";
    }return "HOST KEY ERROR";
}
}
