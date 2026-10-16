#!/usr/bin/env python3
"""Prepare private HTTPS settings and optionally install the user's macOS agent.
No OAuth tokens are read or copied. No API/model calls or keys are created.
"""
import argparse
import ipaddress
import json
import os
from pathlib import Path
import plistlib
import secrets
import shutil
import socket
import subprocess
import sys

PROJECT=Path(__file__).resolve().parents[1]


def private_write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
    if path.is_symlink():
        raise ValueError('Refusing a symlink destination')
    temporary=path.with_name(path.name+'.new')
    with temporary.open('xb') as f:
        f.write(data);f.flush();os.fsync(f.fileno())
    temporary.chmod(0o600);temporary.replace(path);path.chmod(0o600)


def address():
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
        s.connect(('192.0.2.1',9));host=s.getsockname()[0]
    if not ipaddress.ip_address(host).is_private:
        raise ValueError('No private LAN address available')
    return host


def prepare(output,host,port,legacy):
    output.mkdir(parents=True,exist_ok=True,mode=0o700);output.chmod(0o700)
    codex=shutil.which('codex')
    if not codex:
        raise ValueError('Codex CLI is required; sign in with codex login')
    settings=output/'bridge.json'
    if settings.exists():
        config=json.loads(settings.read_text())
        if config['listen']!=host or config['port']!=port:
            raise ValueError('Existing bridge address differs; preserve it and review before regeneration')
    else:
        ca_config=output/'ca.cnf';leaf_config=output/'server.cnf'
        private_write(ca_config,b'[req]\ndistinguished_name=dn\nx509_extensions=ca\nprompt=no\n[dn]\nCN=CYD Usage private CA\n[ca]\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\nsubjectKeyIdentifier=hash\n')
        private_write(leaf_config,('[req]\ndistinguished_name=dn\nprompt=no\n[dn]\nCN=CYD Usage bridge\n[server]\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\nsubjectAltName=IP:'+host+',IP:127.0.0.1\n').encode())
        def openssl(*args):
            subprocess.run(['openssl',*map(str,args)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        previous=os.umask(0o077)
        try:
            openssl('req','-x509','-newkey','rsa:2048','-nodes','-days','1825','-config',ca_config,'-keyout',output/'ca.key','-out',output/'usage-ca.pem')
            openssl('req','-new','-newkey','rsa:2048','-nodes','-config',leaf_config,'-keyout',output/'server.key','-out',output/'server.csr')
            openssl('x509','-req','-in',output/'server.csr','-CA',output/'usage-ca.pem','-CAkey',output/'ca.key','-CAcreateserial','-days','825','-extfile',leaf_config,'-extensions','server','-out',output/'server.pem')
        finally:
            os.umask(previous)
        config={'listen':host,'port':port,'token':secrets.token_urlsafe(32),'codex_bin':codex,'certificate':str(output/'server.pem'),'key':str(output/'server.key'),'feeds':[]}
        private_write(settings,(json.dumps(config,indent=2)+'\n').encode())
    device=output/'usage.json'
    if not device.exists():
        ollama=json.loads(legacy.read_text()).get('api_key','') if legacy.exists() else ''
        providers=[]
        if ollama:
            providers.append({'type':'ollama','id':'ollama','label':'Ollama','api_key':ollama})
        providers.append({'type':'codex','id':'codex','label':'Codex','url':f'https://{host}:{port}/v1/providers/codex','api_key':config['token'],'ca_file':'/private/usage-ca.pem'})
        private_write(device,(json.dumps({'providers':providers},indent=2)+'\n').encode())
    return settings


def agent(settings,output):
    if sys.platform!='darwin':
        raise ValueError('Use your OS service manager for non-macOS installation')
    destination=Path.home()/'Library/LaunchAgents/au.bespoke.cyd.usage.plist'
    script=Path(__file__).with_name('usage_bridge.py')
    args=[sys.executable,str(script),'--config',str(settings)]
    if destination.exists():
        existing=plistlib.loads(destination.read_bytes())
        if existing.get('ProgramArguments')!=args:
            raise ValueError('An unrelated Usage agent exists; no overwrite performed')
    body={'Label':'au.bespoke.cyd.usage','ProgramArguments':args,'RunAtLoad':True,'KeepAlive':True,'ThrottleInterval':30,'StandardOutPath':'/dev/null','StandardErrorPath':'/dev/null','WorkingDirectory':'/','EnvironmentVariables':{'PATH':'/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin'}}
    private_write(destination,plistlib.dumps(body))
    domain=f'gui/{os.getuid()}';label=domain+'/au.bespoke.cyd.usage'
    exists=subprocess.run(['launchctl','print',label],capture_output=True).returncode==0
    if not exists:
        subprocess.run(['launchctl','bootstrap',domain,str(destination)],check=True,capture_output=True)
    subprocess.run(['launchctl','kickstart',label],check=True,capture_output=True)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=PROJECT/'artifacts/provisioning');p.add_argument('--address');p.add_argument('--port',type=int,default=8769);p.add_argument('--legacy-ollama',type=Path,default=PROJECT.parent/'ollama-cloud-usage/artifacts/provisioning/ollama.json');p.add_argument('--install-agent',action='store_true');a=p.parse_args()
    try:
        if not 1024<=a.port<=65535:
            raise ValueError('Port outside permitted range')
        host=a.address or address();settings=prepare(a.output.resolve(),host,a.port,a.legacy_ollama)
        if a.install_agent:
            agent(settings,a.output.resolve())
        print('USAGE_SETUP private configuration and CA ready; OAuth remains in Codex.')
        print('Agent installed.' if a.install_agent else 'Agent not installed; prepared files only.')
    except Exception:
        raise SystemExit('Usage setup could not complete; inspect private settings without sharing credentials.')


if __name__=='__main__':
    main()
