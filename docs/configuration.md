# Private SD configuration

Do not commit your SD card, credentials or keys. Example addresses below are
reserved documentation addresses; they are not reachable devices.

## Terminal Micro

Copy `examples/terminal-config.example.json` to **`/terminal-micro/config.json`**
on your FAT32 card, then edit it privately. Set Wi-Fi (up to four profiles),
SSH hosts (up to eight), port, username and the server's verified SHA256 host
fingerprint. The parser rejects unknown/duplicate fields and changed pins.
A broken app-specific file is rejected, not silently replaced by a fallback.

Use a dedicated unencrypted RSA 3072-bit PEM key for this device. Generate it
on your trusted computer, install only its **public** key in the intended
server account's `authorized_keys`, and copy the private PEM to the SD path
in `auth.private_key_file`. Do not change unrelated server services or keys.
Example generation (local private folder, outside this checkout):

```sh
umask 077
ssh-keygen -t rsa -b 3072 -m PEM -f cyd-terminal -N '' -C cyd-terminal
```

Verify the host fingerprint on the server or through another trusted channel;
`ssh-keyscan` alone is not identity verification. For example, on the server:
`ssh-keygen -lf /etc/ssh/ssh_host_ecdsa_key.pub -E sha256` (the accepted client
requires an ECDSA nistp256 host key). Do not substitute the fingerprint of
a different host key. Never
turn off pinning to fix a connection error. See the client's method preferences
in `common/terminal-core/ssh_client.cpp` for the exact supported algorithms.

**Compatibility detail:** accepted 0.2.3 still requires the private-key directory
`/contextual-micro/ssh_keys/` and the JSON token `ui.theme: "contextual"`.
These are historical data-schema identifiers, not another installed app.
That app is not included or required. Keep these strings for this release;
renaming them would break the shipped parser. The preferred config location
is `/terminal-micro/config.json`; a legacy fallback path also exists in code.
A FAT SD card does not enforce private permissions: treat it as sensitive and
revoke this dedicated server key if the device/card is lost.

## File Server

Use its **Networks → Setup hotspot** flow to join a new 2.4 GHz LAN. From a
phone/computer, join the displayed protected hotspot, open the displayed
address, enter the code and select/configure the destination Wi-Fi network.
The hotspot stops once LAN association succeeds. No enterprise EAP or captive
portal browser is implemented. The firmware supports open and personal
WPA/WPA2/WPA3 profiles subject to the access point/ESP32 capabilities.

Profiles persist in alternating `/file-server/net-a.json` and `net-b.json`
files with a generation counter; do not hand-edit them during a transfer.
A legacy network-config fallback is optional, not required for this app.
The browser code changes on boot. Use the address **and port shown on-device**.
The HTTP service is for trusted local networks: it has no HTTPS protection.
Never port-forward it to the Internet. Only `/files` and `/firmware` are shared. Private config/key areas are
blocked by file policy. Console and Terminal themes are app-owned preferences.
