"""Offline tests of production mail logic; no networking, authentication or emails."""
from pathlib import Path
import subprocess,os,sys
project=Path(__file__).resolve().parents[2];tests=project/'tests/mail';out=project/'.test-output';out.mkdir(exist_ok=True)
env=os.environ.copy();env['ZIG_GLOBAL_CACHE_DIR']=str(project/'.test-cache/mail-global');env['ZIG_LOCAL_CACHE_DIR']=str(project/'.test-cache/mail-local')
compiler=[sys.executable,'-m','ziglang','c++'];flags=['-std=c++11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-I'+str(project/'include')]
def build(name,extra):
 exe=out/name;subprocess.run(compiler+flags+extra+['-o',str(exe)],env=env,cwd=project,check=True);return str(exe)
core=build('mail_core',[str(tests/'test_mail_core.cpp')]);subprocess.run([core],cwd=project,check=True)
sha=build('mail_sha_guard',['-Wno-shift-op-parentheses','-Wno-unused-variable','-I'+str(tests/'sha-stubs'),'-I'+str(project/'src/mining'),str(tests/'test_mail_sha_guard.cpp'),str(project/'src/mining/miner_sha256.cpp')]);subprocess.run([sha],cwd=project,check=True)
settings=build('mail_settings',['-DADVANCED_LOCAL_CONTROLS=1','-DADVANCED_MAIL=1','-I'+str(tests/'controls-stubs'),'-I'+str(project),str(tests/'test_mail_settings.cpp'),str(project/'src/stats/local_settings.cpp')])
for mode in ['save','password','invalid','routes','rollback','mailfail']:subprocess.run([settings,mode],cwd=project,check=True)
print('PASS: all offline mail scenarios; no real message sent')
