import subprocess
import sys
import os

vcvars = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
win_app_dir = os.path.dirname(os.path.abspath(__file__))

cmd = f'call "{vcvars}" && cd /d "{win_app_dir}" && cmake -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release'

print("Starting build...")
proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
while True:
    line = proc.stdout.readline()
    if not line:
        break
    try:
        sys.stdout.write(line.decode('utf-8'))
    except UnicodeDecodeError:
        try:
            sys.stdout.write(line.decode('gbk', errors='replace'))
        except Exception:
            sys.stdout.write(line.decode('latin1', errors='replace'))
    sys.stdout.flush()

proc.wait()

if proc.returncode == 0:
    print("\n[SUCCESS] Build completed successfully!")
    exe_path = os.path.join(win_app_dir, "build", "Release", "CH570_Monitor.exe")
    if os.path.exists(exe_path):
        size_kb = os.path.getsize(exe_path) / 1024.0
        print(f"Binary: {exe_path} ({size_kb:.1f} KB)")
else:
    print(f"\n[FAILED] Process returned code {proc.returncode}")
    sys.exit(proc.returncode)
