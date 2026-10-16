# Firmware distribution

`sd/firmware`: copy contents to the SD `/firmware` directory, retaining data.
`console`: resident factory image plus matching bootstrap assets. Never install
these as ordinary SD apps. See `../docs/install.md` before any USB write.
`SHA256SUMS.txt`: hashes relative to this directory. It covers images, metadata, notices and this README.
Accepted prebuilt versions and source-build differences: `../docs/status.md`.

Usage-0.2.0 is an early-access candidate, not hardware-accepted firmware.
Private keys, device settings and bridge certificates are separate and are not
in this folder. Previous accepted bundles and Ollama 0.1.0 remain in historical
releases. The current app guide is `../projects/usage/README.md`.
