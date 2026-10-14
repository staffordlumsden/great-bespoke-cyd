# Pinned libssh2 ESP-IDF component

Port: skuodi/libssh2_esp tag v1.1.0, commit
198771df0a67156a921a0acf6552d431462a0fdc (BSD-3-Clause).
Upstream libssh2: commit 455f062236fdd5deba9abb909dbe3b86463a23d8,
version 1.11.1. Its licence is preserved as libssh2/COPYING.

Only source/include/CMake support and the port build headers are vendored.
Examples, private configuration, binaries and Git repositories are excluded.
Local build edits scope upstream warning exceptions and compile definitions to
this component. Debug tracing defaults off. No cryptographic source changes.
The port's idf_component.yml declares 1.0.0 despite its v1.1.0 tag; commits above
identify the actual tested source. No PocketSSH application code is included.

ESP32 terminal receive-buffer adaptation: libssh2_priv.h permits a component
compile definition LIBSSH2_RX_STAGING_SIZE=1024. The inbound staging buffer is
1 KiB; MAX_SSH_PACKET_LEN and the outbound buffer remain 35,000 bytes. The ESP32
session allocation changes from 80,712 to 46,736 bytes. No cipher, signature,
MAC, KEX or host-verification implementation was changed. The client restricts
transport to AES-CTR plus SHA-256 MACs. A native OpenSSL-backend integration
test under ASan/UBSan verifies pinned host identity, key authentication and
131,072 output bytes through receive requests at most 1,024 bytes. An initial
unrestricted-algorithm native experiment failed during channel reads; this
adaptation has no AEAD/other-cipher acceptance claim. ESP32 mbedTLS and combined
Bluetooth/Wi-Fi operation require their separate hardware acceptance.

## ESP32 terminal bounded transmit staging

The local `LIBSSH2_TX_STAGING_SIZE=4096` definition reduces the static outgoing buffer independently of the incoming SSH packet limit. `transport.c` validates separate payload lengths before tracing, addition or copying, reserves maximum protocol padding/MAC space, caps the compression destination and checks the final packet size. Oversized messages fail with LIBSSH2_ERROR_INVAL. Terminal channel writes must be chunked to at most 512 bytes. This adaptation is scoped to the configured AES-CTR/no-compression terminal client; it does not establish arbitrary cipher or large-message compatibility.

`mbedtls.c` additionally records the numeric RSA signing return and internal heap counters only in the ESP32 M3 probe. Signing logic, key size, algorithm preferences and trust policy are unchanged. Native sanitizer evidence covers receive assembly and transmit bounds; ESP32 signing acceptance must be recorded separately.
