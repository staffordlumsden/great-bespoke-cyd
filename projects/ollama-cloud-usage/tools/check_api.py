#!/usr/bin/env python3
"""Read only balance/history from Ollama; print schema checks, never values/keys.
Requires the compiled native checker. No inference/model calls are made.
"""
import argparse
import json
import subprocess
import urllib.error
import urllib.request
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=PROJECT/'artifacts/provisioning/ollama.json')
    args = parser.parse_args()
    try:
        config = json.loads(args.config.read_text())
        key = config.get('api_key', '')
        assert isinstance(key, str) and 16 <= len(key) <= 512 and all(33 <= ord(c) <= 126 for c in key)
    except Exception:
        raise SystemExit('Private configuration is missing or invalid; values withheld.')
    check = PROJECT/'artifacts/native/check'
    if not check.is_file():
        raise SystemExit('Run tests/run-native.sh first.')
    validation = subprocess.run([str(check), '--private-config'], input=args.config.read_bytes(), capture_output=True)
    if validation.returncode != 0:
        raise SystemExit('Private configuration does not pass the firmware parser; values withheld.')
    report = {'private_config': {'firmware_parser_ok': True}}

    opener = urllib.request.build_opener(NoRedirect())
    for kind, path in [('balance', 'balance'), ('usage', 'usage?range=24h&scope=self')]:
        request = urllib.request.Request('https://ollama.com/api/'+path, headers={
            'Authorization': 'Bearer '+key, 'Accept': 'application/json'})
        try:
            with opener.open(request, timeout=25) as response:
                payload = response.read(24577)
                # Actual firmware parser with ASan/UBSan, payload on stdin only.
                result = subprocess.run([str(check), '--parse', kind], input=payload, capture_output=True)
                report[kind] = {'status': response.status, 'firmware_parser_ok': result.returncode == 0}
        except urllib.error.HTTPError as exc:
            report[kind] = {'status': exc.code}
        except Exception as exc:
            report[kind] = {'error': type(exc).__name__}
    output = PROJECT/'artifacts/api-check.json'
    output.write_text(json.dumps(report, indent=2)+'\n')
    output.chmod(0o600)
    print(json.dumps(report, indent=2))
    return not all(report[k].get('status') == 200 and report[k].get('firmware_parser_ok') for k in ('balance', 'usage'))


if __name__ == '__main__':
    raise SystemExit(main())
