"""Real loopback HTTP tests, inert captured data, mocked device process only."""
from copy import deepcopy
import importlib.util
import json
import logging
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
import sys
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "startech_raid"))
spec = importlib.util.spec_from_file_location("private_server", ROOT / "startech_raid/server.py")
app = importlib.util.module_from_spec(spec)
spec.loader.exec_module(app)
SAMPLE = json.loads((ROOT / "tests/fixtures/private_app_sample.json").read_text())


class Tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)
        self.identity = app.persistent_identity(self.folder)
        self.now = 2000000000
        self.config = {"interval_seconds":300,"enable_smart":True,"temperature_warning_c":65,"log_level":"info"}
        self.state = app.State(self.config, self.folder, self.identity, lambda:self.now)
        self.state.accept(deepcopy(SAMPLE), 100)
        self.servers = []

    def tearDown(self):
        for server in self.servers:
            server.shutdown();server.server_close()
        self.temp.cleanup()

    def server(self, mode, proxy="127.0.0.1"):
        server = app.Server(("127.0.0.1",0), app.handler(self.state,mode,proxy))
        threading.Thread(target=server.serve_forever,daemon=True).start()
        self.servers.append(server)
        return f"http://127.0.0.1:{server.server_port}"

    def get(self, base, path, auth=None, method="GET"):
        req=Request(base+path,headers={"Authorization":auth} if auth is not None else {},method=method)
        try:
            with urlopen(req,timeout=3) as response:return response.status,response.read(),response.headers
        except HTTPError as exc:return exc.code,exc.read(),exc.headers

    def test_api_auth_and_scope(self):
        base=self.server("api")
        for auth in [None,"","Bearer wrong","Bearer "+"0"*64]:
            self.assertEqual(self.get(base,"/v1/status",auth)[0],401)
        token="Bearer "+self.identity["api_token"]
        code,body,headers=self.get(base,"/v1/status",token)
        self.assertEqual(code,200)
        value=json.loads(body)
        self.assertTrue(value["smart_complete"])
        self.assertNotIn("transfers",value)
        self.assertNotIn(self.identity["api_token"],body.decode())
        self.assertEqual(headers["Cache-Control"],"no-store")
        for path in ["/api/config","/api/logs","/../identity.json","/v1/status?address=0xaed","/"]:
            self.assertEqual(self.get(base,path,token)[0],404)
        self.assertEqual(self.get(base,"/v1/status",token,"POST")[0],405)

    def test_ingress_rejects_nonproxy(self):
        base=self.server("ingress","172.30.32.2")
        self.assertEqual(self.get(base,"/api/config")[0],403)

    def test_ingress_pages_and_routes(self):
        base=self.server("ingress")
        for path in ["/","/ui.js","/style.css","/api/status","/api/config","/api/logs","/api/history","/api/docs"]:
            code,body,headers=self.get(base,path)
            self.assertEqual(code,200,path)
            self.assertIn("nosniff",headers["X-Content-Type-Options"])
        config=json.loads(self.get(base,"/api/config")[1])
        self.assertEqual(config["api_token"],self.identity["api_token"])
        self.assertEqual(self.get(base,"/../../etc/passwd")[0],404)

    def test_stale_and_clock_reversal(self):
        self.now+=901
        data=self.state.snapshot()
        self.assertFalse(data["query_ok"])
        self.assertEqual(data["raid_status"],"Unknown")
        self.assertTrue(all(not d["identity_verified"] and d["temperature_candidate_c"] is None for d in data["ssds"]))
        self.now=1999999999
        self.assertFalse(self.state.snapshot()["fresh"])

    def test_failure_replaces_good(self):
        self.state.accept(app.unknown("mock failure"),50)
        data=self.state.snapshot()
        self.assertFalse(data["query_ok"])
        self.assertEqual(data["raid_status"],"Unknown")
        self.assertEqual(len(data["known_ssds"]),2)

    def test_duplicate_suppresses_health(self):
        sample=deepcopy(SAMPLE);sample["duplicate_ssd_identity"]=True
        self.state.accept(sample,50)
        self.assertTrue(all(not d["identity_verified"] for d in self.state.snapshot()["ssds"]))

    def test_persistent_identity_and_safe_restart(self):
        self.assertEqual(app.persistent_identity(self.folder),self.identity)
        self.assertEqual((self.folder/"identity.json").stat().st_mode & 0o777,0o600)
        restarted=app.State(self.config,self.folder,self.identity,lambda:self.now)
        self.assertFalse(restarted.snapshot()["query_ok"])
        self.assertEqual(len(restarted.snapshot()["known_ssds"]),2)

    def test_options_validation(self):
        path=self.folder/"options.json"
        for value in [{"interval_seconds":59},{"interval_seconds":3601},{"interval_seconds":True},{"enable_smart":"yes"},{"temperature_warning_c":101},{"log_level":"anything"},[]]:
            path.write_text(json.dumps(value))
            with self.assertRaises(ValueError):app.settings(path)

    def test_schema_validation(self):
        for change in [dict(query_ok=False),dict(raid_status="Failed"),dict(ssds=[{}]),dict(ssds="bad")]:
            with self.assertRaises(ValueError):app.validate_sample(dict(deepcopy(SAMPLE),**change))
        sample=deepcopy(SAMPLE);sample["ssds"][0]["temperature_candidate_c"]=float("nan")
        with self.assertRaises(ValueError):app.validate_sample(sample)
        sample=deepcopy(SAMPLE);sample["ssds"][0]["attributes"][1]["id"]=1
        with self.assertRaises(ValueError):app.validate_sample(sample)

    def test_collect_fixed_command_and_errors(self):
        calls=[]
        def run(args,**kwargs):
            calls.append((args,kwargs));return subprocess.CompletedProcess([],0,json.dumps(SAMPLE),"")
        data=app.collect(True,run)
        self.assertTrue(data["query_ok"])
        self.assertEqual(calls[0][0],[app.BINARY,"status","--json","--smart"])
        self.assertNotIn("shell",calls[0][1])
        self.assertEqual(calls[0][1]["timeout"],40)
        for runner in [lambda *a,**k:subprocess.CompletedProcess([],0,"{}",""),
                       lambda *a,**k:subprocess.CompletedProcess([],2,json.dumps(SAMPLE),""),
                       lambda *a,**k:subprocess.CompletedProcess([],3,json.dumps(SAMPLE),""),
                       lambda *a,**k:(_ for _ in ()).throw(subprocess.TimeoutExpired("test",40))]:
            self.assertFalse(app.collect(True,runner)["query_ok"])

    def test_worker_latches_smart_and_waits(self):
        calls=[]
        class Stop:
            cycles=0
            def is_set(self):return self.cycles>=2
            def wait(self,seconds):
                self.cycles+=1
                if seconds!=300:raise AssertionError(seconds)
        def collect(smart):
            calls.append(smart)
            return app.unknown("Mock failed process")
        app.worker(self.state,Stop(),logging.getLogger("test_null"),collect)
        self.assertEqual(calls,[True,False])
        self.assertTrue(self.state.snapshot()["service"]["smart_paused"])

    def test_ring_history_limit(self):
        for i in range(300):
            self.state.history.append({"time":i})
        self.assertEqual(len(self.state.history),288)

    def test_full_private_storage_does_not_kill_cached_service(self):
        with patch.object(app,"atomic_json",side_effect=OSError("Mock full disk")):
            self.state.accept(deepcopy(SAMPLE),50)
        self.assertTrue(self.state.snapshot()["query_ok"])
        self.assertEqual(self.state.snapshot()["service"]["persistence_error"],"OSError")
        self.state.accept(deepcopy(SAMPLE),50)
        self.assertIsNone(self.state.snapshot()["service"]["persistence_error"])


# Additional tests run through the real cached State path.
def test_smart_labels_and_stale_warnings(self):
    sample = deepcopy(SAMPLE)
    disk = sample["ssds"][0]
    disk.update(firmware="Y0507A0", smart_controller_signature="SMI2259XT")
    disk["attributes"] = [dict(id=i, raw=0, normalized=100, worst=100, threshold=50, flags=50, raw_hex="000000000000")
                          for i in [5,160,161,163,168,169,175,176,178,181,182,196,197,198,199]]
    next(a for a in disk["attributes"] if a["id"] == 169)["raw"] = 20
    self.state.accept(sample, 100)
    current = self.state.snapshot()["ssds"][0]
    self.assertTrue(current["warning_indicators"]["life_warning"])
    self.assertFalse(current["warning_indicators"]["media_error_warning"])
    self.assertEqual(current["attributes"][0]["label"], "Reallocated sectors")
    self.now += 901
    stale = self.state.snapshot()["ssds"][0]
    self.assertIsNone(stale["warning_indicators"]["life_warning"])
    self.assertFalse(stale["attributes"][0]["profile_matched"])
Tests.test_smart_labels_and_stale_warnings = test_smart_labels_and_stale_warnings

if __name__=="__main__":unittest.main()
