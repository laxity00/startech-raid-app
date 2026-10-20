"""Read-only StarTech service. Python standard library only.

The worker alone invokes the fixed original binary. HTTP only serves cached
data; it cannot select a device, CDB, file path or command, or trigger self-tests.
"""
from collections import deque
from copy import deepcopy
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hashlib
import hmac
from smart import metadata, indicators
import json
import logging
from logging.handlers import RotatingFileHandler
import math
import os
from pathlib import Path
import secrets
import signal
import socket
import subprocess
import tempfile
import threading
import time
import uuid

VERSION = "0.3.5"
BINARY = "/usr/local/bin/startech-raid-status"
EXPECTED_BINARY_SHA256 = "BUILD_BACKEND_SHA256"
SUPERVISOR_IP = "172.30.32.2"
LABELS = {1: "Healthy", 2: "Degraded", 3: "Failed", 4: "Rebuilding"}
ROOT = Path(__file__).resolve().parent


def settings(path):
    data = json.loads(path.read_text())
    if not isinstance(data, dict):
        raise ValueError("App options must be an object")
    result = {"interval_seconds": data.get("interval_seconds", 300),
              "enable_smart": data.get("enable_smart", True),
              "temperature_warning_c": data.get("temperature_warning_c", 65),
              "log_level": data.get("log_level", "info")}
    for key, low, high in [("interval_seconds", 60, 3600), ("temperature_warning_c", 20, 100)]:
        if type(result[key]) is not int or not low <= result[key] <= high:
            raise ValueError(f"{key} must be an integer from {low} to {high}")
    if type(result["enable_smart"]) is not bool or result["log_level"] not in ("info", "debug", "warning"):
        raise ValueError("Invalid SMART or logging option")
    return result


def atomic_json(path, value, mode=0o600):
    fd, name = tempfile.mkstemp(prefix=".startech-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            os.fchmod(stream.fileno(), mode)
            json.dump(value, stream, allow_nan=False, separators=(",", ":"))
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def persistent_identity(folder):
    path = folder / "identity.json"
    if path.exists():
        data = json.loads(path.read_text())
        uuid.UUID(data["instance_id"])
        token = data["api_token"]
        if not isinstance(token, str) or len(token) != 64 or any(c not in "0123456789abcdef" for c in token):
            raise ValueError("Invalid saved API credential; restore identity.json from backup")
        os.chmod(path, 0o600)
        return data
    data = {"instance_id": str(uuid.uuid4()), "api_token": secrets.token_hex(32)}
    atomic_json(path, data)
    return data


def unknown(reason):
    return {"schema": 1, "query_ok": False, "raid_raw": None, "raid_status": "Unknown",
            "raid_display": "Unknown - query failed", "smart_complete": False,
            "smart_requested": False, "duplicate_ssd_identity": False,
            "ssds": [], "transfers": [], "error": reason}


def validate_sample(value):
    if (not isinstance(value, dict) or value.get("schema") != 1
            or type(value.get("query_ok")) is not bool
            or type(value.get("smart_complete")) is not bool
            or not isinstance(value.get("ssds"), list)):
        raise ValueError("Invalid monitor schema")
    if value["query_ok"]:
        raw = value.get("raid_raw")
        if type(raw) is not int or not 0 <= raw <= 255 or value.get("raid_status") != LABELS.get(raw, "Unknown"):
            raise ValueError("Inconsistent RAID value/label")
    elif value.get("raid_raw") is not None or value.get("raid_status") != "Unknown":
        raise ValueError("Failed query contains a successful RAID state")
    if len(value["ssds"]) not in (0, 2):
        raise ValueError("Expected two port records")
    for index, disk in enumerate(value["ssds"]):
        if not isinstance(disk, dict) or disk.get("port") != index:
            raise ValueError("Incorrect port ordering")
        if type(disk.get("identity_verified")) is not bool:
            raise ValueError("Missing drive identity verification")
        if disk["identity_verified"] and (not isinstance(disk.get("serial"), str) or not disk["serial"]):
            raise ValueError("Missing verified serial")
        temp = disk.get("temperature_candidate_c")
        if temp is not None and (type(temp) not in (int, float) or not math.isfinite(temp) or not 0 < temp < 126):
            raise ValueError("Invalid temperature")
        attrs = disk.get("attributes", [])
        ids = set()
        if not isinstance(attrs, list) or len(attrs) > 30:
            raise ValueError("Invalid SMART attribute table")
        for attr in attrs:
            if not isinstance(attr, dict) or type(attr.get("id")) is not int or not 1 <= attr["id"] <= 255 or attr["id"] in ids:
                raise ValueError("Invalid or duplicate SMART attribute")
            ids.add(attr["id"])
    return value


def collect(smart, runner=subprocess.run):
    args = [BINARY, "status", "--json"] + (["--smart"] if smart else [])
    try:
        proc = runner(args, capture_output=True, text=True, timeout=40, check=False)
        if proc.returncode not in (0, 3, 4, 7) or len(proc.stdout) > 131072:
            raise ValueError("Monitor command failed or output too large")
        value = validate_sample(json.loads(proc.stdout))
        if proc.returncode == 3 and value["query_ok"]:
            raise ValueError("Identity gate failure cannot yield valid data")
        value["collector_exit_code"] = proc.returncode
    except (OSError, subprocess.TimeoutExpired, ValueError, TypeError) as exc:
        value = unknown(str(exc))
    value["sample_time_unix"] = int(time.time())
    return value


class MemoryLog(logging.Handler):
    def __init__(self, state):
        super().__init__()
        self.state = state

    def emit(self, record):
        with self.state.lock:
            self.state.logs.append(self.format(record))


class State:
    def __init__(self, config, folder, identity, clock=time.time):
        self.config, self.folder, self.identity, self.clock = config, folder, identity, clock
        self.lock = threading.RLock()
        self.logs, self.history = deque(maxlen=200), deque(maxlen=288)
        self.sample = unknown("Waiting for first scheduled collection")
        self.sample["sample_time_unix"] = 0
        self.known = {}
        self.smart_paused = False
        self.next_run = 0
        self.duration_ms = None
        self.last_success = None
        self.persistence_error = None
        path = folder / "known_ssds.json"
        if path.exists():
            cached = json.loads(path.read_text())
            if not isinstance(cached, dict):
                raise ValueError("Invalid saved SSD identities")
            self.known = cached

    def accept(self, value, duration_ms):
        value = validate_sample(value)
        value["sample_time_unix"] = int(self.clock())
        with self.lock:
            self.duration_ms = round(duration_ms, 2)
            if value.get("smart_requested") and not value["smart_complete"]:
                self.smart_paused = True
            if value["query_ok"]:
                self.last_success = value["sample_time_unix"]
                for disk in value["ssds"]:
                    if disk["identity_verified"] and not value.get("duplicate_ssd_identity"):
                        self.known[disk["serial"]] = {k: disk.get(k) for k in ("serial", "model", "firmware", "capacity_bytes", "port")}
            self.sample = deepcopy(value)
            self.next_run = self.clock() + self.config["interval_seconds"]
            compact = self.snapshot()
            self.history.append({"time": value["sample_time_unix"], "raid": compact["raid_status"],
                                 "ssds": [{"serial": d.get("serial"), "temperature": d.get("temperature_candidate_c")}
                                          for d in compact["ssds"] if d.get("identity_verified")]})
            identities = deepcopy(self.known)
        try:
            atomic_json(self.folder / "known_ssds.json", identities)
            atomic_json(self.folder / "latest.json", value)
            self.persistence_error = None
        except OSError as exc:
            # Continue serving fresh in-memory state if private storage fills.
            self.persistence_error = type(exc).__name__

    def snapshot(self):
        with self.lock:
            data = deepcopy(self.sample)
            data.pop("transfers", None)  # Raw sectors never travel to Core/Recorder.
            age = self.clock() - data["sample_time_unix"]
            ttl = max(180, 3 * self.config["interval_seconds"])
            data["fresh"] = 0 <= age < ttl and data["sample_time_unix"] > 0
            if not data["fresh"]:
                data.update(query_ok=False, raid_raw=None, raid_status="Unknown", raid_display="Unknown - stale sample", error="No fresh sample")
            # Do not leave healthy drive values usable after identity/query failure.
            if not data["fresh"] or not data["query_ok"] or data.get("duplicate_ssd_identity"):
                for disk in data["ssds"]:
                    disk.update(identity_verified=False, temperature_candidate_c=None, attribute_check="Unknown")
            for disk in data["ssds"]:
                for attr in disk.get("attributes", []):
                    attr.update(metadata(disk, attr["id"]))
                disk["warning_indicators"] = indicators(disk)
            data.update(api_version=1, app_version=VERSION, instance_id=self.identity["instance_id"],
                        known_ssds=list(deepcopy(self.known).values()),
                        service={"interval_seconds": self.config["interval_seconds"],
                                 "smart_enabled": self.config["enable_smart"], "smart_paused": self.smart_paused,
                                 "next_collection_unix": int(self.next_run), "duration_ms": self.duration_ms,
                                 "last_success_unix": self.last_success,
                                 "persistence_error": self.persistence_error,
                                 "temperature_warning_c": self.config["temperature_warning_c"], "stale_after_seconds": ttl})
            return data


def configure_logging(state):
    logger = logging.getLogger("startech_private")
    logger.handlers.clear()
    logger.setLevel(getattr(logging, state.config["log_level"].upper()))
    fmt = logging.Formatter("%(asctime)s %(levelname)s %(message)s", datefmt="%Y-%m-%dT%H:%M:%S%z")
    for handler in (logging.StreamHandler(), RotatingFileHandler(state.folder / "app.log", maxBytes=1048576, backupCount=2), MemoryLog(state)):
        handler.setFormatter(fmt)
        logger.addHandler(handler)
    return logger


def worker(state, stop, logger, collector=collect):
    while not stop.is_set():
        start = time.monotonic()
        try:
            smart = state.config["enable_smart"] and not state.smart_paused
            value = collector(smart)
            if smart and not value.get("smart_complete"):
                state.smart_paused = True
            state.accept(value, 1000 * (time.monotonic() - start))
            warning_keys = sorted({key for disk in state.snapshot()["ssds"]
                                   for key, active in disk.get("warning_indicators", {}).items() if active is True})
            if warning_keys:
                logger.warning("SSD warning indicators: %s; inferred meanings; counters may be historical", ", ".join(warning_keys))
            log = logger.info if not warning_keys and value.get("query_ok") and value.get("raid_raw") == 1 and not state.persistence_error else logger.warning
            log("RAID %s | SMART %s | %s", value.get("raid_display", "Unknown"),
                        "complete" if value.get("smart_complete") else "partial/off",
                        value.get("error") or "collection completed")
            logger.debug("Fixed transfers in sample: %s; collection %.2f ms", len(value.get("transfers", [])), state.duration_ms)
            if state.persistence_error:
                logger.warning("Private state persistence unavailable: %s", state.persistence_error)
            if smart and state.smart_paused:
                logger.warning("SMART polling paused after incomplete sample; inspect port errors and restart app to retry")
        except Exception as exc:
            # No tight retries, no last-good reuse, no user-controlled commands.
            if state.config["enable_smart"]:
                state.smart_paused = True
            state.accept(unknown("Collection failed: " + type(exc).__name__), 1000 * (time.monotonic() - start))
            logger.error("Collection failed: %s", type(exc).__name__)
        stop.wait(state.config["interval_seconds"])


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def handler(state, mode, trusted_proxy=SUPERVISOR_IP):
    class Handler(BaseHTTPRequestHandler):
        server_version = "StarTechMonitor/" + VERSION

        def setup(self):
            super().setup()
            self.connection.settimeout(10)

        def log_message(self, *_):
            pass  # No request headers, credentials, URLs or query strings in logs.

        def reply(self, code, value, mime="application/json"):
            body = value if isinstance(value, bytes) else json.dumps(value, allow_nan=False).encode()
            self.send_response(code)
            self.send_header("Content-Type", mime + "; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'self'")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if mode == "ingress":
                if self.client_address[0] != trusted_proxy:
                    return self.reply(403, {"error": "Supervisor ingress required"})
            else:
                auth = self.headers.get("Authorization", "")
                if not hmac.compare_digest(auth.encode("utf-8"), ("Bearer " + state.identity["api_token"]).encode("utf-8")):
                    return self.reply(401, {"error": "Invalid API token"})
            # Exact path whitelist: no arbitrary file reads, query strings or commands.
            path = self.path
            if mode == "api":
                if path != "/v1/status":
                    return self.reply(404, {"error": "Unknown endpoint"})
                return self.reply(200, state.snapshot())
            if path in ("/", "/index.html", "/ui.js", "/style.css"):
                name = "index.html" if path == "/" else path[1:]
                mime = {"index.html": "text/html", "ui.js": "application/javascript", "style.css": "text/css"}[name]
                return self.reply(200, (ROOT / "web" / name).read_bytes(), mime)
            if path == "/api/status":
                return self.reply(200, state.snapshot())
            if path == "/api/config":
                return self.reply(200, {"options": state.config, "api_token": state.identity["api_token"],
                                        "host": socket.gethostname().replace("_", "-"), "port": 8098,
                                        "hardware": "System disk: full_access requires Protection mode off; AppArmor on; fixed /dev/sda read-only; USB 174c:55aa; firmware 161028_B5_30_81"})
            if path == "/api/logs":
                with state.lock:
                    return self.reply(200, {"lines": list(state.logs)})
            if path == "/api/history":
                with state.lock:
                    return self.reply(200, {"samples": list(state.history)})
            if path == "/api/docs":
                return self.reply(200, {"text": (ROOT / "DOCS.md").read_text()})
            return self.reply(404, {"error": "Unknown endpoint"})

        def do_POST(self):
            self.reply(405, {"error": "Read-only API; use app configuration and restart for changes"})

    return Handler


def main():
    folder = Path("/data")
    state = State(settings(folder / "options.json"), folder, persistent_identity(folder))
    logger = configure_logging(state)
    actual = hashlib.sha256(Path(BINARY).read_bytes()).hexdigest()
    if actual != EXPECTED_BINARY_SHA256:
        raise RuntimeError("Bundled fixed-read binary hash mismatch; refusing device access")
    stop = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stop.set())
    servers = [Server(("0.0.0.0", 8099), handler(state, "ingress")), Server(("0.0.0.0", 8098), handler(state, "api"))]
    for server in servers:
        threading.Thread(target=server.serve_forever, daemon=True).start()
    thread = threading.Thread(target=worker, args=(state, stop, logger), daemon=True)
    thread.start()
    logger.info("App %s started; fixed-read backend hash checked; internal API ready", VERSION)
    stop.wait()
    for server in servers:
        server.shutdown()
        server.server_close()
    thread.join(timeout=45)


if __name__ == "__main__":
    main()
