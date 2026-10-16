#!/usr/bin/env python3
"""Read-only live checks through the actual native firmware parser. No values/keys printed."""
import argparse
import json
from pathlib import Path
import ssl
import subprocess
import urllib.request
import urllib.error
PROJECT=Path(__file__).resolve().parents[1]
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args):return None

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--config',type=Path,default=PROJECT/'artifacts/provisioning/usage.json');a=p.parse_args()
    checker=PROJECT/'artifacts/native/check';raw=a.config.read_bytes()
    if subprocess.run([str(checker),'--private-config'],input=raw,capture_output=True).returncode:
        raise SystemExit('Private configuration did not pass the firmware parser; details withheld.')
    c=json.loads(raw);profiles=c.get('providers',[{'type':'ollama','id':'ollama','api_key':c.get('api_key','')}]);report={}
    for index,profile in enumerate(profiles):
        kind=profile['type'];key=profile.get('api_key','');identifier=profile.get('id',kind)
        if len(key)<16 or 'REPLACE' in key or 'YOUR_' in key:
            report[identifier]={'configured':False};continue
        if kind=='ollama':calls=[('https://ollama.com/api/balance','balance'),('https://ollama.com/api/usage?range=24h&scope=self','usage')]
        elif kind=='openrouter':calls=[('https://openrouter.ai/api/v1/key','openrouter')]
        else:calls=[(profile['url'],'feed')]
        entries=[]
        for url,mode in calls:
            try:
                ca=profile.get('ca_file');ctx=ssl.create_default_context(cafile=a.config.parent/Path(ca).name) if ca else ssl.create_default_context()
                op=urllib.request.build_opener(NoRedirect(),urllib.request.ProxyHandler({}),urllib.request.HTTPSHandler(context=ctx))
                request=urllib.request.Request(url,headers={'Authorization':'Bearer '+key,'Accept':'application/json'})
                with op.open(request,timeout=25) as r:
                    body=r.read(24577);parsed=subprocess.run([str(checker),'--parse',mode,'codex' if kind=='codex' else identifier],input=body,capture_output=True)
                    entries.append({'status':r.status,'firmware_parser_ok':parsed.returncode==0})
            except urllib.error.HTTPError as e:entries.append({'status':e.code})
            except Exception as e:entries.append({'error':type(e).__name__})
        report[identifier]={'checks':entries}
    out=PROJECT/'artifacts/live-source-checks.json';out.write_text(json.dumps(report,indent=2)+'\n');out.chmod(0o600)
    print(json.dumps(report,indent=2))
    return any(any(e.get('status')!=200 or not e.get('firmware_parser_ok') for e in item.get('checks',[])) for item in report.values())
if __name__=='__main__':raise SystemExit(main())
