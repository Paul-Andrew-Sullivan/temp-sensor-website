"""HTTP API for the thermometer. Standard library only.

Run:  python3 server.py
Env:  PORT (8080), DATA_DIR (./data), PROBE_TOKEN, RESEND_API_KEY,
      ALERT_FROM, STATIC_DIR
      (optional: serve a site folder alongside the API for local checks).
"""

import json
import mimetypes
import os
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from alerts import AlertConfig, AlertMonitor, resend_sender
from state import ProbeState


class App:
    """Everything the handler needs, behind one lock."""

    def __init__(self, data_dir, probe_token, sender, static_dir=None):
        os.makedirs(data_dir, exist_ok=True)
        self.lock = threading.Lock()
        self.data_dir = data_dir
        self.alerts_path = os.path.join(data_dir, "alerts.json")
        self.probe_token = probe_token
        self.static_dir = static_dir
        self.state = ProbeState()
        self.alert_config = AlertConfig.load(self.alerts_path)
        self.monitor = AlertMonitor(self.alert_config, sender)
        self.started = time.time()

    # -- sampling loop ----------------------------------------------------

    def sample_once(self, now=None):
        now = time.time() if now is None else now
        with self.lock:
            self.state.tick(now)

    def run_sampler(self):
        while True:
            t0 = time.time()
            try:
                self.sample_once(t0)
            except Exception as exc:  # keep the loop alive whatever happens
                print(f"sampler error: {exc}", flush=True)
            time.sleep(max(0.0, 1.0 - (time.time() - t0)))

    def _ingest(self, s1, s2, b1, b2, now):
        reply = self.state.ingest(s1, s2, b1, b2, now)
        for i, temp in enumerate(self.state.temps):
            self.monitor.check(i + 1, temp, now)
        return reply

    # -- API operations (each returns (status, body)) ---------------------

    def get_state(self):
        with self.lock:
            return 200, self.state.state(time.time())

    def get_history(self):
        with self.lock:
            return 200, self.state.history(time.time())

    def post_button(self, data):
        try:
            sensor = int(data.get("sensor"))
            on = bool(data.get("on"))
        except (TypeError, ValueError, AttributeError):
            return 400, {"error": "body must be {sensor: 1|2, on: true|false}"}
        with self.lock:
            try:
                return 200, self.state.press(sensor, on, now=time.time())
            except ValueError as exc:
                return 400, {"error": str(exc)}

    def get_alerts(self):
        with self.lock:
            body = self.alert_config.to_dict()
            body["lastSent"] = self.monitor.last_sent
            return 200, body

    def put_alerts(self, data):
        if not isinstance(data, dict):
            return 400, {"error": "body must be an object"}
        with self.lock:
            try:
                self.alert_config.update(data)
            except ValueError as exc:
                return 400, {"error": str(exc)}
            self.alert_config.save(self.alerts_path)
            self.monitor.config_changed()
            body = self.alert_config.to_dict()
            body["lastSent"] = self.monitor.last_sent
            return 200, body

    def post_ingest(self, data, token):
        if not self.probe_token or token != self.probe_token:
            return 401, {"error": "bad or missing X-Probe-Token"}
        if not isinstance(data, dict):
            return 400, {"error": "body must be an object"}
        with self.lock:
            reply = self._ingest(data.get("s1"), data.get("s2"), data.get("b1", True), data.get("b2", True), time.time())
            return 200, reply

    def healthz(self):
        with self.lock:
            return 200, {"ok": True, "uptime": int(time.time() - self.started), "box": self.state.box_on(time.time())}


class Handler(BaseHTTPRequestHandler):
    app = None  # set by make_server
    server_version = "thermo/1.0"

    def log_message(self, fmt, *args):
        if os.environ.get("QUIET"):
            return
        BaseHTTPRequestHandler.log_message(self, fmt, *args)

    # -- helpers ----------------------------------------------------------

    def _json(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0:
            return {}
        if length > 64 * 1024:
            return None
        raw = self.rfile.read(length)
        try:
            return json.loads(raw or b"{}")
        except json.JSONDecodeError:
            return None

    def _static(self, path):
        root = self.app.static_dir
        if not root:
            return self._json(404, {"error": "not found"})
        if path == "/":
            path = "/index.html"
        full = os.path.realpath(os.path.join(root, path.lstrip("/")))
        if not full.startswith(os.path.realpath(root) + os.sep) or not os.path.isfile(full):
            return self._json(404, {"error": "not found"})
        ctype = mimetypes.guess_type(full)[0] or "application/octet-stream"
        if full.endswith(".glb"):
            ctype = "model/gltf-binary"
        if full.endswith(".js"):
            ctype = "text/javascript"
        with open(full, "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    # -- routes -----------------------------------------------------------

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/api/state":
            return self._json(*self.app.get_state())
        if path == "/api/history":
            return self._json(*self.app.get_history())
        if path == "/api/alerts":
            return self._json(*self.app.get_alerts())
        if path == "/healthz":
            return self._json(*self.app.healthz())
        if path.startswith("/api/"):
            return self._json(404, {"error": "not found"})
        return self._static(path)

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        data = self._body()
        if data is None:
            return self._json(400, {"error": "body must be JSON"})
        if path == "/api/button":
            return self._json(*self.app.post_button(data))
        if path == "/ingest":
            return self._json(*self.app.post_ingest(data, self.headers.get("X-Probe-Token")))
        return self._json(404, {"error": "not found"})

    def do_PUT(self):
        path = self.path.split("?", 1)[0]
        data = self._body()
        if data is None:
            return self._json(400, {"error": "body must be JSON"})
        if path == "/api/alerts":
            return self._json(*self.app.put_alerts(data))
        return self._json(404, {"error": "not found"})


def make_server(app, host="0.0.0.0", port=8080):
    handler = type("BoundHandler", (Handler,), {"app": app})
    return ThreadingHTTPServer((host, port), handler)


def main():
    data_dir = os.environ.get("DATA_DIR", "./data")
    token = os.environ.get("PROBE_TOKEN", "")
    if not token:
        token = secrets.token_urlsafe(16)
        print(f"PROBE_TOKEN not set; using a random one for this run: {token}", flush=True)
    sender = resend_sender(os.environ.get("RESEND_API_KEY", ""), os.environ.get("ALERT_FROM", "thermo@paulandrewsullivan.com"))
    app = App(
        data_dir=data_dir,
        probe_token=token,
        sender=sender,
        static_dir=os.environ.get("STATIC_DIR") or None,
    )
    threading.Thread(target=app.run_sampler, daemon=True).start()
    port = int(os.environ.get("PORT", "8080"))
    httpd = make_server(app, port=port)
    print(f"thermo api on :{port}", flush=True)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
