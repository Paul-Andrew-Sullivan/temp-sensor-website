import json
import os
import tempfile
import unittest

from alerts import AlertConfig, AlertMonitor


class FakeSender:
    def __init__(self, ok=True):
        self.sent = []
        self.ok = ok

    def __call__(self, to, subject, body):
        self.sent.append((to, subject, body))
        return self.ok


def make(email="me@example.com", mx=30.0, mn=15.0):
    cfg = AlertConfig(email=email, max=mx, min=mn,
                      max_message="Too warm in the lab", min_message="Too cold in the lab")
    sender = FakeSender()
    return AlertMonitor(cfg, sender), sender, cfg


class Thresholds(unittest.TestCase):
    def test_fires_once_on_crossing_max(self):
        mon, sender, _ = make()
        self.assertIsNone(mon.check(1, 25.0, now=1.0))
        self.assertEqual(mon.check(1, 30.5, now=2.0), "max")
        self.assertIsNone(mon.check(1, 31.0, now=3.0))
        self.assertIsNone(mon.check(1, 35.0, now=4.0))
        self.assertEqual(len(sender.sent), 1)
        to, subject, body = sender.sent[0]
        self.assertEqual(to, "me@example.com")
        self.assertIn("Too warm in the lab", body)
        self.assertIn("30.5", body)
        self.assertIn("ensor 1", body)

    def test_rearms_after_hysteresis(self):
        mon, sender, _ = make()
        mon.check(1, 31.0, now=1.0)
        self.assertIsNone(mon.check(1, 29.8, now=2.0))  # inside band but within 0.5 of max
        self.assertIsNone(mon.check(1, 30.2, now=3.0))
        self.assertIsNone(mon.check(1, 29.4, now=4.0))  # now below 29.5, rearmed
        self.assertEqual(mon.check(1, 30.1, now=5.0), "max")
        self.assertEqual(len(sender.sent), 2)

    def test_fires_on_min(self):
        mon, sender, _ = make()
        self.assertEqual(mon.check(2, 14.0, now=1.0), "min")
        self.assertIn("Too cold in the lab", sender.sent[0][2])
        self.assertIn("ensor 2", sender.sent[0][2])

    def test_sensors_are_independent(self):
        mon, sender, _ = make()
        self.assertEqual(mon.check(1, 31.0, now=1.0), "max")
        self.assertEqual(mon.check(2, 31.0, now=1.0), "max")
        self.assertEqual(len(sender.sent), 2)

    def test_none_never_fires(self):
        mon, sender, _ = make()
        self.assertIsNone(mon.check(1, None, now=1.0))
        self.assertEqual(sender.sent, [])

    def test_blank_email_disables(self):
        mon, sender, _ = make(email="")
        self.assertIsNone(mon.check(1, 99.0, now=1.0))
        self.assertEqual(sender.sent, [])

    def test_last_sent_recorded(self):
        mon, sender, _ = make()
        mon.check(1, 31.0, now=123.0)
        self.assertEqual(mon.last_sent["sensor"], 1)
        self.assertEqual(mon.last_sent["kind"], "max")
        self.assertEqual(mon.last_sent["t"], 123000)
        self.assertTrue(mon.last_sent["ok"])

    def test_config_change_resets_latches(self):
        mon, sender, cfg = make()
        mon.check(1, 31.0, now=1.0)
        cfg.max = 35.0
        mon.config_changed()
        self.assertIsNone(mon.check(1, 31.0, now=2.0))
        self.assertEqual(mon.check(1, 36.0, now=3.0), "max")


class ConfigFile(unittest.TestCase):
    def test_round_trip_and_defaults(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "alerts.json")
            cfg = AlertConfig.load(path)
            self.assertEqual(cfg.email, "")
            self.assertEqual(cfg.max, 30.0)
            self.assertEqual(cfg.min, 15.0)
            cfg.email = "a@b.c"
            cfg.max = 40.0
            cfg.save(path)
            again = AlertConfig.load(path)
            self.assertEqual(again.email, "a@b.c")
            self.assertEqual(again.max, 40.0)
            self.assertEqual(json.load(open(path))["max"], 40.0)

    def test_update_from_dict_validates(self):
        cfg = AlertConfig()
        cfg.update({"email": " x@y.z ", "max": "33", "min": 10, "maxMessage": "hi", "minMessage": "lo"})
        self.assertEqual(cfg.email, "x@y.z")
        self.assertEqual(cfg.max, 33.0)
        self.assertEqual(cfg.max_message, "hi")
        with self.assertRaises(ValueError):
            cfg.update({"max": 5, "min": 10})
        with self.assertRaises(ValueError):
            cfg.update({"max": "warm"})
        self.assertEqual(cfg.to_dict()["maxMessage"], "hi")


if __name__ == "__main__":
    unittest.main()
