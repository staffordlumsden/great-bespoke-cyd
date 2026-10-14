#!/usr/bin/env python3
"""Create a new CYD example project; never install, flash or overwrite output."""
import argparse
import os
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]

def create(project, output):
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9]{0,19}', project) or project == 'cyd_launcher':
        raise ValueError('Use a distinct ASCII project name, 1-20 letters/digits')
    output = output.resolve()
    if not output.is_relative_to(ROOT) or output.exists():
        raise ValueError('Output must be a new directory inside the repository')
    fw=output/'firmware';main=fw/'main';main.mkdir(parents=True)
    root_rel=Path(os.path.relpath(ROOT,fw)).as_posix()
    include_rel=Path(os.path.relpath(ROOT/'projects/launcher/firmware/main',main)).as_posix()
    cmake=f'''cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.1.0")
set(CYD_ROOT "${{CMAKE_CURRENT_LIST_DIR}}/{root_rel}")
set(EXTRA_COMPONENT_DIRS
    "${{CYD_ROOT}}/projects/launcher/firmware/components/cyd_hal"
    "${{CYD_ROOT}}/projects/launcher/firmware/components/cyd_clock")
include($ENV{{IDF_PATH}}/tools/cmake/project.cmake)
project({project})
'''
    (fw/'CMakeLists.txt').write_text(cmake)
    (main/'CMakeLists.txt').write_text(f'''idf_component_register(SRCS "app_main.cpp" "console_bridge.cpp"
    INCLUDE_DIRS "." "{include_rel}"
    REQUIRES cyd_hal cyd_clock nvs_flash app_update esp_app_format)
''')
    defaults=(ROOT/'projects/launcher/firmware/sdkconfig.defaults').read_text().replace('"../../../partitions.csv"',f'"{root_rel}/partitions.csv"')
    (fw/'sdkconfig.defaults').write_text(defaults)
    for suffix in ('h','cpp'):
        source=ROOT/f'projects/terminal-micro/firmware/main/console_bridge.{suffix}'
        (main/f'console_bridge.{suffix}').write_text(source.read_text().replace('terminal','myapp').replace('TM_BOOT','EXAMPLE_BOOT').replace('TM_CONSOLE','EXAMPLE_CONSOLE'))
    source=(ROOT/'spec/examples/minimal-app.cpp').read_text()
    (main/'app_main.cpp').write_text(source.replace('__TITLE__',project).replace('__NVS__',('ex_'+project.lower())[:15]))
    tool=fw/'tools';tool.mkdir()
    build=(ROOT/'projects/terminal-micro/firmware/tools/build.sh').read_text().replace('$firmware/../../..',f'$firmware/{root_rel}').replace('Terminal Micro',project)
    (tool/'build.sh').write_text(build);(tool/'build.sh').chmod(0o755)
    # Multi-project dependency rights must be reviewed when extending this scaffold.
    (output/'README.md').write_text(f'''# {project}

Generated scaffold, version 0.1.0; NOT hardware accepted. Theme and Console
controls only, no radio provisioning. Build from repo root with:

```sh
bash {output.relative_to(ROOT).as_posix()}/firmware/tools/build.sh build
```

Use root AGENTS.md, spec/design.md and the release checklist. Do not flash the
image over the factory Console. Package image/JSON plus applicable notices.
''')
    return output

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    print('CREATED',create(args.project,args.output))
