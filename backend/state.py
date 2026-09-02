"""In-memory state for the two-sensor thermometer.

One ProbeState holds the latest report from the box, the wanted button
states, and a 300-sample ring buffer per sensor. Temperatures are Celsius.
"""

from collections import deque

HISTORY_LEN = 300

# DS18B20 error readings. -127 = no device on the bus, 85 = power-on reset
# value returned before a conversion has completed.
SENSOR_ERROR_VALUES = (-127.0, 85.0)


def _clean(temp):
    """Return the reading as a float, or None if it means 'unplugged'."""
    if temp is None:
        return None
    try:
        t = float(temp)
    except (TypeError, ValueError):
        return None
    if t != t:  # NaN
        return None
    for bad in SENSOR_ERROR_VALUES:
        if abs(t - bad) < 0.01:
            return None
    return t


class ProbeState:
    def __init__(self, history_len=HISTORY_LEN, box_timeout=3.0):
        self.history_len = history_len
        self.box_timeout = box_timeout
        self.last_report = None  # unix seconds of last ingest, None if never
        self.temps = [None, None]
        self.board_buttons = [True, True]  # what the board last said
        self.wanted_buttons = [True, True]  # what the computer asked for
        # A virtual press is "pending" until the board echoes it back or the
        # deadline passes. Outside a pending window the board is the truth,
        # so a physical press on the box shows up here too.
        self.pending_until = [None, None]
        self.press_timeout = 2.0
        self.hist = [deque([None] * history_len, maxlen=history_len) for _ in range(2)]
        self.last_tick = None

    # -- reports from the board -------------------------------------------

    def ingest(self, s1, s2, b1, b2, now):
        self.last_report = now
        self.temps = [_clean(s1), _clean(s2)]
        self.board_buttons = [bool(b1), bool(b2)]
        for i in range(2):
            deadline = self.pending_until[i]
            if deadline is None:
                self.wanted_buttons[i] = self.board_buttons[i]
            elif self.board_buttons[i] == self.wanted_buttons[i] or now > deadline:
                self.pending_until[i] = None
                self.wanted_buttons[i] = self.board_buttons[i]
        return {"b1": self.wanted_buttons[0], "b2": self.wanted_buttons[1]}

    def box_on(self, now):
        return self.last_report is not None and (now - self.last_report) <= self.box_timeout

    # -- sampling ---------------------------------------------------------

    def tick(self, now):
        """Append one sample per sensor. Call once a second."""
        on = self.box_on(now)
        for i in range(2):
            self.hist[i].append(self.temps[i] if on else None)
        self.last_tick = now

    # -- virtual buttons --------------------------------------------------

    def press(self, sensor, on, now=None):
        if sensor not in (1, 2):
            raise ValueError("sensor must be 1 or 2")
        i = sensor - 1
        self.wanted_buttons[i] = bool(on)
        base = now if now is not None else (self.last_report or 0.0)
        self.pending_until[i] = base + self.press_timeout
        return {"b1": self.wanted_buttons[0], "b2": self.wanted_buttons[1]}

    # -- views ------------------------------------------------------------

    def _sensor_view(self, i, on):
        temp = self.temps[i] if on else None
        return {
            "temp": temp,
            "plugged": (self.temps[i] is not None) if on else False,
            "on": self.wanted_buttons[i],
        }

    def state(self, now):
        on = self.box_on(now)
        return {
            "box": "on" if on else "off",
            "t": int(now * 1000),
            "s1": self._sensor_view(0, on),
            "s2": self._sensor_view(1, on),
        }

    def history(self, now):
        return {
            "t": int((self.last_tick if self.last_tick is not None else now) * 1000),
            "s1": list(self.hist[0]),
            "s2": list(self.hist[1]),
        }
