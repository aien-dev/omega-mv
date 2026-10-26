#!/usr/bin/env python3
"""TEST-ONLY equal-work benchmark driver: omagent vs nanobot reduced to the same four tools.

Usage: equal_bench.py OMAGENT VENV_PYTHON N CPU DELAY_MS OUT.tsv
Both arms get the same prompt, endpoint, scripted mock (equal_mock.py), streaming, workspace
contents and tool script (read_file, write_file, one /bin/echo through each harness's shell tool,
final answer). Each run is a fresh process with a fresh session: nanobot's session store is
deleted between runs, omagent runs without --session. Client pinned with taskset to CPU, the
mock to CPU+1. Arms alternate order every repetition (paired, interleaved).
Row: arm cpu delay_ms rep wall_ns first_req_ns maxrss_kib rc posts verdict
first_req_ns = mock arrival of the first POST minus the driver's launch time (CLOCK_MONOTONIC).
verdict is ok only if the mock saw all four steps with correct tool results, out.txt holds
beta-456 and stdout holds delta-000."""
import json, os, shutil, subprocess, sys, time, urllib.request

B, PY, N, CPU, DELAY, OUT = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5], sys.argv[6]
D = os.path.dirname(os.path.abspath(__file__))
H = "/tmp/OmegaBot-eq-%d-%s" % (CPU, DELAY)
PROMPT = "Read notes.txt, write beta-456 to out.txt, run /bin/echo gamma-789, then reply done."
shutil.rmtree(H, ignore_errors=True)
os.makedirs(H + "/nb/workspace")
os.makedirs(H + "/om")
pf = H + "/port"
mock = subprocess.Popen(["taskset", "-c", str(CPU + 1), sys.executable, D + "/equal_mock.py", pf, DELAY])
for _ in range(100):
    if os.path.exists(pf) and open(pf).read():
        break
    time.sleep(0.05)
P = int(open(pf).read())
assert P != 18091
URL = "http://127.0.0.1:%d/v1" % P
cfg = H + "/nb/config.json"
json.dump({
    "agents": {"defaults": {"workspace": H + "/nb/workspace", "model": "mock-1", "provider": "custom",
                            "timezone": "UTC", "maxToolIterations": 8,
                            "disabledSkills": ["clawhub", "cron", "github", "memory", "skill-creator", "summarize",
                                               "tmux", "weather", "my", "image-generation", "cli-apps"]}},
    "providers": {"custom": {"apiKey": "sk-test", "apiBase": URL}},
    "tools": {"restrictToWorkspace": True, "mcpServers": {},
              "exec": {"enable": True, "timeout": 10},
              "web": {"enable": True, "fetch": {"useJinaReader": False}},
              "file": {"enable": True},
              "cliApps": {"enable": False}, "my": {"enable": False}, "imageGeneration": {"enabled": False}},
}, open(cfg, "w"))
ENV = dict(os.environ, HOME=H + "/nb", OMAGENT_KEY="sk-test")
# Peak RSS comes from GNU time, whose child is forked from a small C process; the driver's own
# rusage would charge the forked Python driver image to the child before exec.
TIME = ["/usr/bin/time", "-f", "%M", "-o", H + "/rss"]
CMD = {
    "omagent": (TIME + ["taskset", "-c", str(CPU), B, "--url", URL + "/chat/completions", "--model", "mock-1",
                 "--key-env", "OMAGENT_KEY", "-p", PROMPT, "--workdir", H + "/om", "--allow-write",
                 "--allow-cmd", "/bin/echo", "--cmd-timeout", "10", "--max-rounds", "8"], H + "/om"),
    "nanobot": (TIME + ["taskset", "-c", str(CPU), PY, D + "/nb_minimal.py", "agent", "-m", PROMPT, "--no-markdown",
                 "-c", cfg, "-w", H + "/nb/workspace"], H + "/nb/workspace"),
}


def state():
    return json.load(urllib.request.urlopen("http://127.0.0.1:%d/state" % P))


def one(arm):
    cmd, wd = CMD[arm]
    for f in ("out.txt",):
        if os.path.exists(os.path.join(wd, f)):
            os.unlink(os.path.join(wd, f))
    open(os.path.join(wd, "notes.txt"), "w").write("alpha-123\n")
    shutil.rmtree(H + "/nb/sessions", ignore_errors=True)
    mem = H + "/nb/workspace/memory"
    if os.path.isdir(H + "/memory.snap"):
        shutil.rmtree(mem, ignore_errors=True)
        shutil.copytree(H + "/memory.snap", mem)
    state()
    so = open(H + "/stdout", "w")
    t0 = time.monotonic_ns()
    p = subprocess.Popen(cmd, cwd=wd, env=ENV, stdout=so, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
    _, status, ru = os.wait4(p.pid, 0)
    t1 = time.monotonic_ns()
    p.returncode = os.waitstatus_to_exitcode(status)
    so.close()
    st = state()
    out = open(H + "/stdout").read()
    okf = os.path.exists(os.path.join(wd, "out.txt")) and open(os.path.join(wd, "out.txt")).read().strip() == "beta-456"
    ok = p.returncode == 0 and st["posts"] == 4 and not st["bad"] and okf and "delta-000" in out
    verdict = "ok" if ok else "BAD:" + json.dumps({"posts": st["posts"], "bad": st["bad"], "file": okf, "out": out[-200:]})
    rss = int(open(H + "/rss").read().split()[-1])
    return [t1 - t0, (st["first_ns"] - t0) if st["first_ns"] else -1, rss, p.returncode, st["posts"], verdict, st["tools"], st["req_bytes"]]


with open(OUT, "w") as fh:
    for arm in ("omagent", "nanobot"):  # warm-up, not recorded; also records the advertised tools
        r = one(arm)
        print("warmup", arm, r[3], r[5], "tools=" + ",".join(r[6]), "req_bytes=%d" % r[7], flush=True)
        if arm == "nanobot":
            shutil.copytree(H + "/nb/workspace/memory", H + "/memory.snap")
    for rep in range(1, N + 1):
        order = ("omagent", "nanobot") if rep % 2 else ("nanobot", "omagent")
        for arm in order:
            r = one(arm)
            fh.write("\t".join(map(str, [arm, CPU, DELAY, rep] + r[:6] + [r[7]])) + "\n")
            fh.flush()
mock.kill()
