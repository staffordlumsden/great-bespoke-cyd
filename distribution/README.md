# Firmware distribution

`sd/firmware`: copy contents to the SD `/firmware` directory, retaining data.
`console`: resident factory image plus matching bootstrap assets. Never install
these as ordinary SD apps. See `../docs/install.md` before any USB write.
`SHA256SUMS.txt`: hashes relative to this directory. It covers images, metadata, notices and this README.
Accepted prebuilt versions and source-build differences: `../docs/status.md`.

OllamaUsage-0.1.0 is an early-access candidate, not hardware-accepted firmware.
Its private API configuration is deliberately separate and is never distributed.
The earlier accepted release ZIP is retained unchanged; the Ollama app also has
its own prerelease download. See `../projects/ollama-cloud-usage/README.md`.
