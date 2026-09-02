"""Threshold alerts: configuration, latching monitor, and a Resend sender."""

import json
import os
import urllib.request
from dataclasses import dataclass, asdict

HYSTERESIS_C = 0.5


@dataclass
class AlertConfig:
    email: str = ""
    max: float = 30.0
    min: float = 15.0
    max_message: str = "Temperature is above the limit."
    min_message: str = "Temperature is below the limit."

    # The wire format uses camelCase keys, matching the pages.
    def to_dict(self):
        return {
            "email": self.email,
            "max": self.max,
            "min": self.min,
            "maxMessage": self.max_message,
            "minMessage": self.min_message,
        }

    def update(self, data):
        """Apply a partial update from a dict. Raises ValueError on bad input."""
        new = AlertConfig(**asdict(self))
        if "email" in data:
            new.email = str(data["email"] or "").strip()[:200]
        for key, attr in (("max", "max"), ("min", "min")):
            if key in data:
                try:
                    setattr(new, attr, float(data[key]))
                except (TypeError, ValueError):
                    raise ValueError(f"{key} must be a number")
        if "maxMessage" in data:
            new.max_message = str(data["maxMessage"] or "")[:500]
        if "minMessage" in data:
            new.min_message = str(data["minMessage"] or "")[:500]
        if new.max <= new.min:
            raise ValueError("max must be greater than min")
        self.__dict__.update(new.__dict__)

    @classmethod
    def load(cls, path):
        cfg = cls()
        try:
            with open(path) as f:
                raw = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            return cfg
        try:
            cfg.update(raw)
        except ValueError:
            pass
        return cfg

    def save(self, path):
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.to_dict(), f, indent=2)
        os.replace(tmp, path)


class AlertMonitor:
    """Latches per sensor so each crossing sends exactly one message."""

    def __init__(self, config, sender):
        self.config = config
        self.sender = sender
        self.latched = {1: None, 2: None}  # None | "max" | "min"
        self.last_sent = None

    def config_changed(self):
        self.latched = {1: None, 2: None}

    def check(self, sensor, temp, now):
        if temp is None or not self.config.email:
            return None
        cfg = self.config
        latched = self.latched[sensor]

        if latched == "max" and temp < cfg.max - HYSTERESIS_C:
            self.latched[sensor] = None
            latched = None
        elif latched == "min" and temp > cfg.min + HYSTERESIS_C:
            self.latched[sensor] = None
            latched = None

        if latched is not None:
            return None

        kind = None
        if temp > cfg.max:
            kind = "max"
        elif temp < cfg.min:
            kind = "min"
        if kind is None:
            return None

        self.latched[sensor] = kind
        message = cfg.max_message if kind == "max" else cfg.min_message
        limit = cfg.max if kind == "max" else cfg.min
        subject = f"Thermometer alert: sensor {sensor} {'above' if kind == 'max' else 'below'} {limit:g} C"
        body = (
            f"{message}\n\n"
            f"Sensor {sensor} read {temp:.1f} C ({temp * 9 / 5 + 32:.1f} F), "
            f"{'above the maximum' if kind == 'max' else 'below the minimum'} of {limit:g} C.\n"
        )
        ok = False
        try:
            ok = bool(self.sender(cfg.email, subject, body))
        except Exception as exc:  # network trouble must not kill the sampler
            print(f"alert send failed: {exc}", flush=True)
        self.last_sent = {"t": int(now * 1000), "sensor": sensor, "kind": kind, "ok": ok, "to": cfg.email}
        return kind


def resend_sender(api_key, from_addr):
    """Return a sender(to, subject, body) that posts to the Resend API."""

    def send(to, subject, body):
        if not api_key:
            print("alert not sent: RESEND_API_KEY is empty", flush=True)
            return False
        payload = json.dumps({
            "from": from_addr,
            "to": [to],
            "subject": subject,
            "text": body,
        }).encode()
        req = urllib.request.Request(
            "https://api.resend.com/emails",
            data=payload,
            method="POST",
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
            },
        )
        with urllib.request.urlopen(req, timeout=10) as resp:
            return 200 <= resp.status < 300

    return send
