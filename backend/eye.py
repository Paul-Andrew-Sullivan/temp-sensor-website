"""In-memory state and alert settings for the electric eye (Lab 2).

The board watches an IR beam and reports the beam state, the envelope level
and its two thresholds. EyeState keeps the latest report, a 300-sample ring
buffer of level and beam, and the transitions seen. Like the thermometer, the
board sends its own alert mail; EyeAlerts only stores what the page edits.
"""

import json
import os
from collections import deque
from dataclasses import dataclass, asdict

HISTORY_LEN = 300
EVENTS_LEN = 50
BEAM_STATES = ("clear", "broken")


def _num(value):
    """Return the value as a float, or None if it is missing or not a number."""
    if value is None or isinstance(value, bool):
        return None
    try:
        v = float(value)
    except (TypeError, ValueError):
        return None
    return None if v != v else v


class EyeState:
    def __init__(self, history_len=HISTORY_LEN, timeout=3.0):
        self.timeout = timeout
        self.last_report = None  # unix seconds of last ingest, None if never
        self.beam = None
        self.level = None
        self.on = None
        self.off = None
        self.breaches = 0
        self.last_alert = None
        self.level_hist = deque([None] * history_len, maxlen=history_len)
        self.beam_hist = deque([None] * history_len, maxlen=history_len)
        self.events = deque(maxlen=EVENTS_LEN)

    def ingest(self, data, now):
        """Store one report. Raises ValueError on a bad beam or breaches field."""
        beam = data.get("beam")
        if beam not in BEAM_STATES:
            raise ValueError('beam must be "clear" or "broken"')
        try:
            breaches = int(data.get("breaches") or 0)
        except (TypeError, ValueError):
            raise ValueError("breaches must be a whole number")
        # The first report after startup is a starting point, not a transition.
        if self.beam is not None and beam != self.beam:
            self.events.append({"t": int(now * 1000), "state": beam})
        self.beam = beam
        self.level = _num(data.get("level"))
        self.on = _num(data.get("on"))
        self.off = _num(data.get("off"))
        self.breaches = breaches
        alert = data.get("lastAlert")
        self.last_alert = alert if isinstance(alert, dict) else None
        self.last_report = now

    def online(self, now):
        return self.last_report is not None and (now - self.last_report) <= self.timeout

    def tick(self, now):
        """Append one sample. Call once a second."""
        on = self.online(now)
        self.level_hist.append(self.level if on else None)
        self.beam_hist.append(self.beam if on else None)

    def view(self, now):
        on = self.online(now)
        return {
            "online": on,
            "t": int(now * 1000),
            "beam": self.beam if on else "unknown",
            "level": self.level if on else None,
            "on": self.on if on else None,
            "off": self.off if on else None,
            "breaches": self.breaches,
            "lastAlert": self.last_alert,
            "lastSeen": int(self.last_report * 1000) if self.last_report is not None else None,
            "history": {"level": list(self.level_hist), "beam": list(self.beam_hist)},
            "events": list(self.events),
        }


@dataclass
class EyeAlerts:
    email: str = ""
    subject: str = "Electric eye: beam broken"
    message: str = "The beam was interrupted."

    def to_dict(self):
        return asdict(self)

    def update(self, data):
        if "email" in data:
            self.email = str(data["email"] or "").strip()[:200]
        if "subject" in data:
            self.subject = str(data["subject"] or "").strip()[:200]
        if "message" in data:
            self.message = str(data["message"] or "").strip()[:500]

    @classmethod
    def load(cls, path):
        cfg = cls()
        try:
            with open(path) as f:
                raw = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            return cfg
        if isinstance(raw, dict):
            cfg.update(raw)
        return cfg

    def save(self, path):
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.to_dict(), f, indent=2)
        os.replace(tmp, path)
