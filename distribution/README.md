# Firmware distribution

`sd/firmware`: copy contents to the SD `/firmware` directory, retaining data.
`console`: resident factory image plus matching bootstrap assets. Never install
these as ordinary SD apps. See `../docs/install.md` before any USB write.
`SHA256SUMS.txt`: hashes relative to this directory. It covers images, metadata, notices and this README.
Accepted prebuilt versions and source-build differences: `../docs/status.md`.

LEDMicro-0.1.0 is user-accepted for its main LED controls and Console/Run last.
Usage-0.2.0 remains early access, with basic SD install/Codex-Ollama switching
user-confirmed and extended checks pending.
Private keys, device settings and bridge certificates are separate and are not
in this folder. Previous accepted bundles and Ollama 0.1.0 remain in historical
releases. The current app guide is `../projects/usage/README.md`.
