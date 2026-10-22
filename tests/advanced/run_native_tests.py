"""Compile and execute the production accounting/HTTP-handler tests on this Mac."""
from pathlib import Path
import os
import subprocess
import sys

project = Path(__file__).resolve().parents[2]
output = project/'.test-output'
output.mkdir(exist_ok=True)
env = os.environ.copy()
env['ZIG_GLOBAL_CACHE_DIR'] = str(project/'.test-cache/zig-global')
env['ZIG_LOCAL_CACHE_DIR'] = str(project/'.test-cache/zig-local')
compiler = [sys.executable, '-m', 'ziglang', 'c++']
flags = ['-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1']
base = project/'tests/advanced'
subprocess.run(compiler+flags+[str(base/'accounting_test.cpp'), '-o', str(output/'accounting_test')], env=env, check=True)
subprocess.run([str(output/'accounting_test'), str(base/'dashboard_fixture.json')], env=env, check=True)
subprocess.run(compiler+flags+['-pthread', '-I'+str(base/'stubs'), '-I'+str(project/'include'),
               str(base/'web_test.cpp'), str(project/'src/stats/advanced_web.cpp'), '-o', str(output/'web_test')], env=env, check=True)
subprocess.run([str(output/'web_test')], env=env, check=True)
