#!/usr/bin/env python3
"""Validate public layout, local links, release hashes and obvious secret leaks."""
from pathlib import Path
import hashlib,json,os,re
ROOT=Path(__file__).resolve().parents[1]
errors=[];count=0
skip={'.git','.tools','node_modules','artifacts','__pycache__'}
for base,dirs,names in os.walk(ROOT):
 dirs[:]=[d for d in dirs if d not in skip and not d.startswith(('build','._'))]
 for name in names:
  if name.startswith('._') or name=='.DS_Store':continue
  p=Path(base)/name;rel=p.relative_to(ROOT).as_posix();count+=1
  if any(part in ('contextual-micro',) for part in p.relative_to(ROOT).parts):errors.append('Excluded app path: '+rel)
  if name.endswith(('.pem','.key')) or (name=='config.json'):errors.append('Private config/key file: '+rel)
  data=p.read_bytes()
  if re.search(rb'-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----\r?\n[A-Za-z0-9+/]{32}',data):errors.append('Private key material: '+rel)
  if name.endswith(('.md','.py','.sh','.cpp','.h','.html','.yml','.json','.cjs','.js')):
   text=data.decode('utf-8',errors='replace')
   for marker in ('/Users/','/Volumes/','192.168.1.109','192.168.1.110','stafford-lumsden@'):
    # The scanner contains its own forbidden strings.
    if marker in text and rel!='tools/validate_public.py':errors.append('Private marker '+marker+' in '+rel)
   if name.endswith('.md') and 'third_party/' not in rel and not rel.startswith('common/libssh2_esp/'):
    for target in re.findall(r'\]\(([^)]+)\)',text):
     target=target.strip('<>').split('#')[0]
     if not target or '://' in target or target.startswith('mailto:'):continue
     if not (p.parent/target).exists():errors.append('Broken local link in '+rel+': '+target)
manifest=ROOT/'distribution/SHA256SUMS.txt'
for line in manifest.read_text().splitlines():
 digest,name=line.split('  ',1);p=manifest.parent/name
 if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=digest:errors.append('Release hash mismatch: '+name)
for p in (ROOT/'distribution/sd/firmware').glob('*.json'):
 if p.name.startswith('._'):continue
 m=json.loads(p.read_text());data=p.with_name(m['image']).read_bytes()
 if len(data)!=m['size_bytes'] or hashlib.sha256(data).hexdigest()!=m['sha256']:errors.append('SD manifest mismatch: '+p.name)
 if len(data)>0x1b0000:errors.append('App too large: '+p.name)
for needed in ('README.md','AGENTS.md','SKILL.md','spec/SPEC.md','spec/reference.md','spec/templates.md','site/index.html','THIRD_PARTY.md'):
 if not (ROOT/needed).is_file():errors.append('Missing '+needed)
if errors:raise SystemExit('\n'.join(errors))
print('PASS public layout, local documentation links, secret markers and release integrity ('+str(count)+' files)')
