import os, pathlib, subprocess, sys
root = pathlib.Path(__file__).resolve().parents[2]
vc = pathlib.Path(r"C:/Program Files/Microsoft Visual Studio/2022/Community/SDK/ScopeCppSDK/vc15/VC")
sdk = vc.parent / "SDK"
env = os.environ.copy()
env["INCLUDE"] = ";".join(map(str,[vc/"include",sdk/"include/um",sdk/"include/shared",sdk/"include/ucrt"]))
env["LIB"] = ";".join(map(str,[vc/"lib",sdk/"lib"]))
out = pathlib.Path(r"E:/Download/AI/hermes/cache/scratch") / "sr5000 provisioning & tests"
out.mkdir(parents=True,exist_ok=True)
lines=[]
def run(cmd):
    r=subprocess.run(cmd,env=env,capture_output=True,text=True)
    output=r.stdout+r.stderr
    lines.append(output)
    print(output,end="",flush=True)
    if r.returncode: raise SystemExit(r.returncode)
    return r
try:
    run([sys.executable,str(root/"tests/provisioning/test_launcher.py")])
    for source,name in [("fake_setupc.cpp","setupc"),("inventory_test.cpp","inventory_test")]:
        cmd=[str(vc/"bin/cl.exe"),"/nologo","/EHsc","/W4","/WX","/std:c++14","/MT","/DUNICODE","/D_UNICODE","/D_WIN32_WINNT=0x0601",str(root/"tests/provisioning"/source),"/Fe:"+str(out/(name+".exe")),"/Fo:"+str(out/(name+".obj")),"/link","/SUBSYSTEM:CONSOLE,6.01"]
        run(cmd)
    # Never invoke a system setupc or the actual GUI. Only this just-built fake.
    run([str(out/"inventory_test.exe"),str(out/"setupc.exe")])
finally:
    (out/"test-output.txt").write_text("".join(lines),encoding="utf-8")
    print("TEST OUTPUT: "+str(out/"test-output.txt"))
