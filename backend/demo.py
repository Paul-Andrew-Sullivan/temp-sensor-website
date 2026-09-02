"""Demo reading generator, used until a real board reports in.

Produces two slow, plausible traces so the pages have something to show.
The scenario picks what the fake board is doing so each display state can
be checked by hand.
"""

import math

SCENARIOS = ("normal", "unplugged1", "unplugged2", "boxoff", "high", "low")


class Demo:
    def __init__(self, scenario="normal"):
        self.on = True
        self._scenario = "normal"
        self.scenario = scenario
        self.started = None

    @property
    def scenario(self):
        return self._scenario

    @scenario.setter
    def scenario(self, value):
        if value not in SCENARIOS:
            raise ValueError(f"unknown scenario {value!r}")
        self._scenario = value
        self.started = None

    def box_absent(self):
        """True when the fake box is switched off, so nothing should be ingested."""
        return self._scenario == "boxoff"

    def _base(self, now):
        # Two traces drifting around room temperature, a few degrees apart,
        # with different periods so they never look like copies.
        s1 = 22.0 + 1.8 * math.sin(now / 47.0) + 0.4 * math.sin(now / 7.3)
        s2 = 20.5 + 1.2 * math.sin(now / 61.0 + 1.1) + 0.3 * math.sin(now / 5.1)
        return round(s1, 1), round(s2, 1)

    def sample(self, now):
        if self.started is None:
            self.started = now
        elapsed = now - self.started
        s1, s2 = self._base(now)
        sc = self._scenario
        if sc == "unplugged1":
            return None, s2
        if sc == "unplugged2":
            return s1, None
        if sc == "boxoff":
            return None, None
        if sc == "high":
            # climb about 0.3 C per second toward 60 C and hold there
            s1 = min(63.0, round(s1 + 0.3 * elapsed, 1))
            s1 = min(s1, 60.0 + 0.4 * math.sin(now / 9.0))
            return round(s1, 1), s2
        if sc == "low":
            s1 = max(-10.0, round(s1 - 0.3 * elapsed, 1))
            s1 = max(s1, 2.0 + 0.4 * math.sin(now / 9.0))
            return round(s1, 1), s2
        return s1, s2
