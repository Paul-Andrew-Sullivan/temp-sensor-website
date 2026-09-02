import json
import os
import tempfile
import threading
import unittest
import urllib.error
import urllib.request

from server import App, make_server


class ServerTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.sent = []
        self.app = App(
            data_dir=self.tmp.name,
            probe_token="secret-token",
            sender=lambda to, subject, body: self.sent.append((to, subject, body)) or True,
            demo_default=True,
        )
        self.httpd = make_server(self.app, host="127.0.0.1", port=0)
        self.port = self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.tmp.cleanup()

    def req(self, method, path, body=None, headers=None):
        data = json.dumps(body).encode() if body is not None else None
        h = {"Content-Type": "application/json"}
        h.update(headers or {})
        r = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}", data=data, method=method, headers=h)
        try:
            with urllib.request.urlopen(r, timeout=5) as resp:
                return resp.status, json.loads(resp.read() or b"null"), dict(resp.headers)
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read() or b"null"), dict(e.headers)

    def test_state_is_json_and_not_cached(self):
        status, body, headers = self.req("GET", "/api/state")
        self.assertEqual(status, 200)
        self.assertIn(body["box"], ("on", "off"))
        self.assertIn("s1", body)
        self.assertTrue(body["demo"])
        self.assertEqual(headers.get("Cache-Control"), "no-store")

    def test_history_has_300_entries(self):
        status, body, _ = self.req("GET", "/api/history")
        self.assertEqual(status, 200)
        self.assertEqual(len(body["s1"]), 300)
        self.assertEqual(len(body["s2"]), 300)

    def test_button_toggles(self):
        status, body, _ = self.req("POST", "/api/button", {"sensor": 2, "on": False})
        self.assertEqual(status, 200)
        self.assertFalse(body["b2"])
        status, body, _ = self.req("POST", "/api/button", {"sensor": 9, "on": False})
        self.assertEqual(status, 400)

    def test_alerts_round_trip_and_persist(self):
        status, body, _ = self.req("GET", "/api/alerts")
        self.assertEqual(status, 200)
        self.assertEqual(body["max"], 30.0)
        status, body, _ = self.req("PUT", "/api/alerts", {"email": "a@b.c", "max": 41, "min": 5, "maxMessage": "hot", "minMessage": "cold"})
        self.assertEqual(status, 200)
        self.assertEqual(body["max"], 41.0)
        with open(os.path.join(self.tmp.name, "alerts.json")) as f:
            self.assertEqual(json.load(f)["email"], "a@b.c")
        status, body, _ = self.req("PUT", "/api/alerts", {"max": 1, "min": 5})
        self.assertEqual(status, 400)
        self.assertIn("error", body)

    def test_ingest_requires_token_and_disables_demo(self):
        status, _, _ = self.req("POST", "/ingest", {"s1": 20.0, "s2": 21.0, "b1": True, "b2": True})
        self.assertEqual(status, 401)
        status, body, _ = self.req("POST", "/ingest", {"s1": 20.0, "s2": 21.0, "b1": True, "b2": True},
                                   headers={"X-Probe-Token": "secret-token"})
        self.assertEqual(status, 200)
        self.assertEqual(body, {"b1": True, "b2": True})
        status, body, _ = self.req("GET", "/api/state")
        self.assertFalse(body["demo"])
        self.assertEqual(body["s1"]["temp"], 20.0)

    def test_ingest_reply_carries_virtual_press(self):
        self.req("POST", "/ingest", {"s1": 20.0, "s2": 21.0, "b1": True, "b2": True}, headers={"X-Probe-Token": "secret-token"})
        self.req("POST", "/api/button", {"sensor": 1, "on": False})
        status, body, _ = self.req("POST", "/ingest", {"s1": 20.0, "s2": 21.0, "b1": True, "b2": True}, headers={"X-Probe-Token": "secret-token"})
        self.assertEqual(body, {"b1": False, "b2": True})

    def test_ingest_triggers_alert(self):
        self.req("PUT", "/api/alerts", {"email": "a@b.c", "max": 30, "min": 10})
        self.req("POST", "/ingest", {"s1": 35.0, "s2": 21.0, "b1": True, "b2": True}, headers={"X-Probe-Token": "secret-token"})
        self.assertEqual(len(self.sent), 1)
        status, body, _ = self.req("GET", "/api/alerts")
        self.assertEqual(body["lastSent"]["sensor"], 1)

    def test_demo_toggle_and_scenario(self):
        status, body, _ = self.req("POST", "/api/demo", {"on": True, "scenario": "boxoff"})
        self.assertEqual(status, 200)
        self.assertEqual(body["scenario"], "boxoff")
        status, body, _ = self.req("POST", "/api/demo", {"scenario": "volcano"})
        self.assertEqual(status, 400)
        status, body, _ = self.req("POST", "/api/demo", {"on": False})
        self.assertFalse(body["on"])

    def test_demo_sampler_populates_state(self):
        self.app.sample_once()
        status, body, _ = self.req("GET", "/api/state")
        self.assertIsNotNone(body["s1"]["temp"])
        self.app.demo.scenario = "boxoff"
        self.app.state.last_report -= 10  # pretend the last report is stale
        self.app.sample_once()
        status, body, _ = self.req("GET", "/api/state")
        self.assertEqual(body["box"], "off")

    def test_unknown_route_404(self):
        status, body, _ = self.req("GET", "/api/nothing")
        self.assertEqual(status, 404)

    def test_healthz(self):
        status, body, _ = self.req("GET", "/healthz")
        self.assertEqual(status, 200)
        self.assertEqual(body["ok"], True)


if __name__ == "__main__":
    unittest.main()
