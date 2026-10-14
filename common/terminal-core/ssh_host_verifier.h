#pragma once
#include <cstddef>
#include <cstdint>
namespace cm {
enum class HostTrustResult {verified,unconfigured,invalid_key,hash_error,mismatch};
// The caller supplies libssh2_session_hostkey's exact raw public key blob,
// never a textual public key, MD5/SHA1 digest, or the user's client key.
HostTrustResult verify_server_key(const uint8_t* raw_key,size_t size,
                                 const uint8_t expected_sha256[32],bool pin_configured);
const char* host_trust_name(HostTrustResult result);
}
