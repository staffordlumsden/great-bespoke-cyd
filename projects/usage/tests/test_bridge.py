#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import json
import math
import ssl
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import urllib.request
import urllib.error

ROOT=Path(__file__).resolve().parents[1]
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/file);value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
bridge=module('usage_bridge','bridge/usage_bridge.py')
installer=module('install_bridge','bridge/install_bridge.py')
TOKEN='public-test-feed-token-1234567890123456'
def fixture():
    return {'rateLimits':{'primary':{'usedPercent':21,'windowDurationMins':300,'resetsAt':int(time.time())+600},'secondary':{'usedPercent':42,'windowDurationMins':10080,'resetsAt':int(time.time())+9000}}}

class Models(unittest.TestCase):
    def test_buckets_and_unknowns(self):
        d=fixture();d['rateLimitsByLimitId']={'codex':d['rateLimits'],'other':{'primary':{'usedPercent':99}}}
        value=bridge.normalize_codex(d);self.assertEqual([m['label'] for m in value['meters']],['5H WINDOW','WEEKLY']);self.assertEqual(value['meters'][0]['used_percent'],21);self.assertEqual(value['stats'],[])
        d['rateLimits']['primary']=None;self.assertEqual(len(bridge.normalize_codex(d)['meters']),1)
        d['rateLimits']['secondary']=None
        with self.assertRaises(bridge.Unavailable):bridge.normalize_codex(d)
    def test_invalid_usage_and_no_credential_fields(self):
        for bad in [-1,101,True,'21',math.inf]:
            d=fixture();d['rateLimits']['primary']['usedPercent']=bad
            with self.assertRaises(bridge.Unavailable):bridge.normalize_codex(d)
        d=fixture();d['accountId']='not-for-the-device';d['tokens']='do-not-forward';d['rateLimits']['credits']={'hasCredits':True,'balance':'12.5'}
        value=bridge.normalize_codex(d);self.assertNotIn('tokens',value);self.assertNotIn('accountId',value);self.assertEqual(value['stats'][0]['value'],'12.5')
        d['rateLimits']['credits']={'hasCredits':False,'balance':'unknown'};self.assertEqual(bridge.normalize_codex(d)['stats'],[])
    def test_feed_freshness_and_sanitizing(self):
        value=bridge.normalize_codex(fixture());value['access_token']='must-drop';self.assertNotIn('access_token',bridge.validate_feed(value,'codex'))
        for delta in [-181,121]:
            value['updated_at']=int(time.time())+delta
            with self.assertRaises(bridge.Unavailable):bridge.validate_feed(value,'codex')
        value=bridge.normalize_codex(fixture());value['meters'][0]['label']='x\n'
        with self.assertRaises(bridge.Unavailable):bridge.validate_feed(value,'codex')

class Transport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='usage-bridge-test-');cls.root=Path(cls.tmp.name)
        with patch.object(installer.shutil,'which',return_value=sys.executable):
            path=installer.prepare(cls.root,'127.0.0.1',8769,cls.root/'absent.json')
        cls.config=json.loads(path.read_text());cls.config['token']=TOKEN
        cls.cache=bridge.Cache(cls.config)
        with patch.object(bridge,'read_codex',return_value=bridge.normalize_codex(fixture())):cls.cache.refresh()
        ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(cls.config['certificate'],cls.config['key'])
        cls.server=bridge.Server(('127.0.0.1',0),bridge.make_handler(TOKEN,cls.cache),ctx)
        cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start()
        cls.base=f'https://127.0.0.1:{cls.server.server_address[1]}'
        cls.ctx=ssl.create_default_context(cafile=cls.root/'usage-ca.pem')
    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown();cls.server.server_close();cls.thread.join(5);cls.tmp.cleanup()
    def request(self,path,token=TOKEN,method='GET'):
        request=urllib.request.Request(self.base+path,headers={'Authorization':'Bearer '+token},method=method)
        opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),urllib.request.HTTPSHandler(context=self.ctx))
        try:
            with opener.open(request,timeout=3) as r:return r.status,json.load(r)
        except urllib.error.HTTPError as e:return e.code,json.load(e)
    def test_https_auth_paths_read_only(self):
        status,value=self.request('/v1/providers/codex');self.assertEqual(status,200);self.assertEqual(len(value['meters']),2)
        self.assertEqual(self.request('/v1/providers/codex','wrong-token')[0],401)
        for path in ['/','/v1/providers/../config','/v1/providers/codex?x=1','/private/usage.json']:
            self.assertEqual(self.request(path)[0],404)
        self.assertEqual(self.request('/v1/providers/codex',method='POST')[0],405)
    def test_unavailable_and_custom_feed(self):
        with self.cache.lock:old=self.cache.values['codex'];self.cache.values['codex']={**old,'updated_at':int(time.time())-190}
        try:self.assertEqual(self.request('/v1/providers/codex')[0],503)
        finally:
            with self.cache.lock:self.cache.values['codex']=old
        local=self.root/'custom.json';value={**old,'provider':'my-ai'};local.write_text(json.dumps(value));self.config['feeds']=[{'id':'my-ai','file':str(local)}]
        self.assertEqual(self.request('/v1/providers/my-ai')[0],200)
        local.write_text(' '*24577);self.assertEqual(self.request('/v1/providers/my-ai')[0],503)
    def test_untrusted_certificate_rejected(self):
        with self.assertRaises(urllib.error.URLError):
            urllib.request.urlopen(self.base+'/v1/providers/codex',context=ssl.create_default_context(),timeout=3)
    def test_cli_protocol_only_uses_read_rpcs(self):
        fake=self.root/'codex-fixture';fake.write_text('#!'+sys.executable+'\n'+'''import sys,json,time
for line in sys.stdin:
 m=json.loads(line);method=m.get('method')
 if method=='initialize':r={}
 elif method=='initialized':continue
 elif method=='account/rateLimits/read':r={'rateLimits':{'primary':{'usedPercent':0,'windowDurationMins':300,'resetsAt':int(time.time())+100}}}
 else:raise SystemExit(8)
 print(json.dumps({'id':m['id'],'result':r}),flush=True)
''');fake.chmod(0o700)
        value=bridge.read_codex(str(fake),timeout=3);self.assertEqual(value['meters'][0]['used_percent'],0)

if __name__=='__main__':unittest.main()
