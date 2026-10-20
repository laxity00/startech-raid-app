"""Offline CLI only: never invokes the hardware status command with valid args."""
import json
import subprocess
import sys

prefix = sys.argv[1:]
count = 0
def run(args, code=0):
    global count
    result = subprocess.run(prefix + args, capture_output=True, text=True)
    count += 1
    assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
    return result.stdout

assert "0.3.4" in run(["--help"])
assert "No hardware access" in run([])
for raw, label in [(0,"Unknown"),(1,"Healthy"),(2,"Degraded"),(3,"Failed"),(4,"Rebuilding"),(5,"Unknown"),(6,"Unknown"),(255,"Unknown")]:
    assert run(["explain",str(raw)]).strip() == f"{raw} - {label}"
for raw in ["-1","256","1.5","","abc","9999999999999999999999999999999999"]:
    run(["explain",raw],2)
for args in [["status","--device","/dev/sdb"],["status","--json","--json"],
             ["status","--smart","--smart"],["status","--address","0x0aed"],
             ["probe","raid-state"],["live-fw-dump"],["status","--force"],
             ["status","--smartctl","/tmp/utility"],["status","--sat12"],["explain"]]:
    run(args,2)
print(json.dumps({"offline_cli_cases": count,"hardware_access":False}))
