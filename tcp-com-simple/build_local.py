"""Run the unchanged build.bat with discovered bundled MSVC x64 SDK.
No install, driver, device, startup or network actions. Optional command args
replace build.bat (used for focused test builds).
"""
import os
from pathlib import Path
import subprocess
import sys

base = Path('C:/Program Files/Microsoft Visual Studio/2022/Community/SDK/ScopeCppSDK/vc15')
env = os.environ.copy()
env['PATH'] = ';'.join([str(base/'VC/bin'), str(base/'SDK/bin'), env['PATH']])
env['INCLUDE'] = ';'.join(str(base/p) for p in ['VC/include', 'SDK/include/ucrt', 'SDK/include/shared', 'SDK/include/um'])
env['LIB'] = ';'.join(str(base/p) for p in ['VC/lib', 'SDK/lib'])
env['TMP'] = env['TEMP'] = 'E:/Download/AI/hermes/cache/scratch'
command = subprocess.list2cmdline(sys.argv[1:]) if len(sys.argv)>1 else 'build.bat'
result = subprocess.run(command, shell=True, cwd=Path(__file__).parent, env=env)
sys.exit(result.returncode)
