#!/usr/bin/env python3
"""Convert native renderer PPMs and copy public browser fixtures after tests."""
from pathlib import Path
import shutil,json,hashlib
from PIL import Image
r=Path(__file__).resolve().parents[1]
def w(p,s):
 p=r/p;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s.strip()+'\n')
shots={'console-dark':'projects/launcher/artifacts/native/console.ppm','console-light':'projects/launcher/artifacts/native/console-light.ppm','sd-library':'projects/launcher/artifacts/native/library.ppm','install-progress':'projects/launcher/artifacts/native/progress.ppm','clock':'projects/launcher/artifacts/native/clock.ppm','file-server-dark':'projects/file-server/artifacts/native/device-dark.ppm','file-server-light':'projects/file-server/artifacts/native/device-light.ppm','terminal-home-dark':'projects/terminal-micro/artifacts/native/page-0-0.ppm','terminal-home-light':'projects/terminal-micro/artifacts/native/page-0-1.ppm','keyboard':'projects/terminal-micro/artifacts/native/page-1-0.ppm','terminal-hosts':'projects/terminal-micro/artifacts/native/page-3-0.ppm','terminal-session':'projects/terminal-micro/artifacts/native/terminal-1.ppm'}
for page in ('legacy','activity','codex','openrouter','sources'):
 for theme in ('dark','light'):shots[f'usage-{page}-{theme}']=f'projects/usage/artifacts/native/{theme}-{page}.ppm'
for page in ('colours','patterns','mixer'):
 for theme in ('dark','light'):shots[f'led-{page}-{theme}']=f'projects/led-micro/artifacts/native/{theme}-{page}.ppm'
for page in ('current','hourly','details','settings','search','results'):
 for theme in ('dark','light'):shots[f'weather-{page}-{theme}']=f'projects/weather-micro/artifacts/native/{theme}-{page}.ppm'
for page in ('editor-dark','editor-light','history','clock-required'):
 shots[f'quicknote-{page}']=f'projects/quicknote-micro/artifacts/native/{page}.ppm'
for name,source in shots.items():
 p=r/f'site/assets/screenshots/{name}.png';p.parent.mkdir(parents=True,exist_ok=True);Image.open(r/source).save(p,optimize=True)
for name in ('browser-dark','browser-light','browser-mobile-light'):shutil.copy2(r/f'projects/file-server/artifacts/browser/{name}.png',r/f'site/assets/screenshots/{name}.png')

print("Screenshots regenerated from public test captures.")
