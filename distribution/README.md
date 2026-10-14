# Firmware distribution

`sd/firmware`: copy contents to the SD `/firmware` directory, retaining data.
`console`: resident factory image plus matching bootstrap assets. Never install
these as ordinary SD apps. See `../docs/install.md` before any USB write.
`SHA256SUMS.txt`: hashes relative to this directory. It covers images, metadata, notices and this README.
Accepted prebuilt versions and source-build differences: `../docs/status.md`.
