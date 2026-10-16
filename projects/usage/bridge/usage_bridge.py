#!/usr/bin/env python3
"""Read-only HTTPS usage feed. OAuth stays in Codex; no thread/model/mutation RPCs.
Python 3.10+, standard library only. Private settings are supplied with --config.
"""
import argparse
import hmac
import http.server
import ipaddress
import json
import math
import os
from pathlib import Path
import re
import selectors
import ssl
import subprocess
import threading
import time

MAX_BODY = 24576


class Unavailable(Exception):
    pass


def finite(value, minimum=0, maximum=1e12):
    return type(value) in (int, float) and math.isfinite(value) and minimum <= value <= maximum


def short_text(value, maximum):
    return isinstance(value, str) and 0 < len(value) <= maximum and all(32 <= ord(c) <= 126 for c in value)


def validate_feed(data, expected):
    if not isinstance(data, dict) or type(data.get('schema')) is not int or data.get('schema') != 1 or data.get('provider') != expected:
        raise Unavailable()
    stamp = data.get('updated_at')
    if not finite(stamp, 1704067200, 4102444799) or stamp > time.time()+120 or stamp < time.time()-180:
        raise Unavailable()
    meters, stats = data.get('meters'), data.get('stats')
    if not isinstance(meters, list) or len(meters)>2 or not isinstance(stats, list) or len(stats)>3 or not (meters or stats):
        raise Unavailable()
    result = {'schema': 1, 'provider': expected, 'updated_at': int(stamp), 'meters': [], 'stats': []}
    for m in meters:
        if not isinstance(m, dict) or not short_text(m.get('label'), 16) or not finite(m.get('used_percent'), 0, 100):
            raise Unavailable()
        reset = m.get('resets_at')
        if reset is not None and (not finite(reset, 1704067200, 4102444799) or int(reset)!=reset):
            raise Unavailable()
        result['meters'].append({'label': m['label'], 'used_percent': m['used_percent'], 'resets_at': reset})
    for s in stats:
        if not isinstance(s, dict) or not short_text(s.get('label'),16) or not short_text(s.get('value'),24):
            raise Unavailable()
        result['stats'].append({'label':s['label'], 'value':s['value']})
    return result


def normalize_codex(result, now=None):
    if not isinstance(result, dict):
        raise Unavailable()
    buckets = result.get('rateLimitsByLimitId')
    limits = buckets.get('codex') if isinstance(buckets, dict) and 'codex' in buckets else result.get('rateLimits')
    if not isinstance(limits, dict):
        raise Unavailable()
    meters = []
    for key in ('primary', 'secondary'):
        item = limits.get(key)
        if item is None:
            continue
        if not isinstance(item, dict) or not finite(item.get('usedPercent'), 0, 100):
            raise Unavailable()
        duration = item.get('windowDurationMins')
        if finite(duration, 1, 1000000) and int(duration) == duration:
            label = '5H WINDOW' if duration == 300 else 'WEEKLY' if duration == 10080 else f'{duration//1440:g}D WINDOW' if duration%1440 == 0 else f'{duration//60:g}H WINDOW' if duration%60 == 0 else f'{duration:g}M WINDOW'
        else:
            label = 'PRIMARY' if key == 'primary' else 'SECONDARY'
        reset = item.get('resetsAt')
        if reset is not None and (not finite(reset, 1704067200, 4102444799) or int(reset)!=reset):
            raise Unavailable()
        meters.append({'label':label,'used_percent':item['usedPercent'],'resets_at':reset})
    if not meters:
        raise Unavailable()
    stats = []
    credits = limits.get('credits')
    if isinstance(credits, dict):
        if credits.get('unlimited') is True:
            stats.append({'label':'CREDITS','value':'Unlimited'})
        elif credits.get('hasCredits') is True:
            try:
                number = float(credits.get('balance'))
            except (TypeError, ValueError):
                number = math.nan
            if finite(number):
                stats.append({'label':'CREDITS','value':f'{number:g}'[:24]})
    return {'schema':1,'provider':'codex','updated_at':int(time.time() if now is None else now),'meters':meters,'stats':stats}


def read_codex(binary='codex', timeout=25):
    # Uses the signed-in CLI's supported RPC. Never reads/copies auth.json.
    child = subprocess.Popen([binary,'app-server','--listen','stdio://'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
    selector = selectors.DefaultSelector()
    selector.register(child.stdout,selectors.EVENT_READ)
    pending = bytearray()
    deadline = time.monotonic()+timeout
    def send(message):
        child.stdin.write(json.dumps(message,separators=(',',':')).encode()+b'\n');child.stdin.flush()
    def reply(identifier):
        while time.monotonic()<deadline:
            while b'\n' in pending:
                line,_,rest=pending.partition(b'\n');pending[:]=rest
                try:
                    message=json.loads(line)
                except (ValueError,UnicodeError):
                    continue
                if message.get('id')==identifier:
                    if 'error' in message:
                        raise Unavailable()
                    return message.get('result')
                # Refuse any unexpected server request; never authorize actions.
                if 'method' in message and 'id' in message:
                    send({'id':message['id'],'error':{'code':-32601,'message':'Read-only usage client'}})
            if not selector.select(min(1,max(0,deadline-time.monotonic()))):
                continue
            data=os.read(child.stdout.fileno(),8192)
            if not data:
                raise Unavailable()
            pending.extend(data)
            if len(pending)>1024*1024:
                raise Unavailable()
        raise Unavailable()
    try:
        send({'id':1,'method':'initialize','params':{'clientInfo':{'name':'cyd_usage','title':'CYD Usage','version':'0.2.0'}}})
        reply(1);send({'method':'initialized'})
        send({'id':2,'method':'account/rateLimits/read','params':{}})
        return normalize_codex(reply(2))
    finally:
        selector.close();child.terminate()
        try:
            child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            child.kill();child.wait(timeout=3)
        child.stdin.close();child.stdout.close()


class Cache:
    def __init__(self, config):
        self.config=config;self.lock=threading.Lock();self.values={};self.stop=threading.Event()
    def refresh(self):
        try:
            value=read_codex(self.config.get('codex_bin','codex'))
            with self.lock:
                self.values['codex']=validate_feed(value,'codex')
        except Exception:
            # Preserve last successful time; an old snapshot becomes unavailable.
            pass
    def run(self):
        while not self.stop.is_set():
            self.refresh();self.stop.wait(60)
    def read(self, provider):
        if provider=='codex':
            with self.lock:
                value=self.values.get(provider)
            if value is None:
                raise Unavailable()
            return validate_feed(value,provider)
        for item in self.config.get('feeds',[]):
            if item.get('id')==provider:
                file=Path(item['file'])
                if file.stat().st_size>MAX_BODY:
                    raise Unavailable()
                with file.open('rb') as source:
                    body=source.read(MAX_BODY+1)
                if len(body)>MAX_BODY:
                    raise Unavailable()
                return validate_feed(json.loads(body),provider)
        raise KeyError(provider)


class Server(http.server.ThreadingHTTPServer):
    daemon_threads=True
    def __init__(self,address,handler,context):
        self.context=context;self.slots=threading.BoundedSemaphore(4);super().__init__(address,handler)
    def handle_error(self,*args):
        pass
    def process_request(self,request,client_address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request);return
        try:
            super().process_request(request,client_address)
        except Exception:
            self.slots.release();raise
    def process_request_thread(self,request,client_address):
        try:
            super().process_request_thread(request,client_address)
        finally:
            self.slots.release()
    def get_request(self):
        connection,address=super().get_request();connection.settimeout(5)
        try:
            return self.context.wrap_socket(connection,server_side=True),address
        except Exception:
            connection.close();raise


def make_handler(token,cache):
    class Handler(http.server.BaseHTTPRequestHandler):
        server_version='UsageBridge';sys_version=''
        def log_message(self,*args):
            pass  # No request headers, account values or client identity in logs.
        def answer(self,status,data):
            body=json.dumps(data,separators=(',',':')).encode()
            self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(body)));self.send_header('Connection','close');self.end_headers()
            try:
                self.wfile.write(body)
            except (OSError,ssl.SSLError):
                pass
            self.close_connection=True
        def do_GET(self):
            auth=self.headers.get_all('Authorization',[])
            if len(auth)!=1 or not hmac.compare_digest(auth[0].encode('utf-8'),('Bearer '+token).encode('ascii')):
                self.answer(401,{'error':'unauthorized'});return
            match=re.fullmatch(r'/v1/providers/([a-z0-9_-]{1,16})',self.path)
            if not match:
                self.answer(404,{'error':'not_found'});return
            try:
                self.answer(200,cache.read(match[1]))
            except KeyError:
                self.answer(404,{'error':'not_found'})
            except Exception:
                self.answer(503,{'error':'usage_unavailable'})
        def do_POST(self):
            self.answer(405,{'error':'read_only'})
        do_PUT=do_POST;do_DELETE=do_POST;do_PATCH=do_POST
    return Handler


def serve(config):
    address=config['listen'];ip=ipaddress.ip_address(address)
    if not (ip.is_private or ip.is_loopback) or ip.is_unspecified or ip.version!=4:
        raise ValueError('An explicit private IPv4 interface is required')
    token=config['token']
    if not isinstance(token,str) or len(token)<32 or not all(33<=ord(c)<=126 for c in token):
        raise ValueError('Invalid feed token')
    ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.minimum_version=ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(config['certificate'],config['key'])
    cache=Cache(config);thread=threading.Thread(target=cache.run,daemon=True);thread.start()
    server=Server((address,int(config.get('port',8769))),make_handler(token,cache),ctx)
    print('USAGE_BRIDGE ready read_only=yes credentials=withheld',flush=True)
    try:
        server.serve_forever(poll_interval=.5)
    finally:
        cache.stop.set();server.server_close()


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--config',type=Path,required=True);args=parser.parse_args()
    try:
        serve(json.loads(args.config.read_text()))
    except KeyboardInterrupt:
        pass
    except Exception:
        raise SystemExit('Usage bridge unavailable; check private settings (details withheld).')


if __name__=='__main__':
    main()
