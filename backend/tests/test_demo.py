import unittest

from demo import Demo, SCENARIOS


class Scenarios(unittest.TestCase):
    def test_normal_is_plausible_room_temperature(self):
        d = Demo()
        for t in range(0, 600, 7):
            s1, s2 = d.sample(now=1000.0 + t)
            self.assertIsNotNone(s1)
            self.assertIsNotNone(s2)
            self.assertTrue(15.0 <= s1 <= 30.0, s1)
            self.assertTrue(15.0 <= s2 <= 30.0, s2)
        self.assertNotEqual(d.sample(now=1000.0)[0], d.sample(now=1000.0)[1])

    def test_unplugged_scenarios(self):
        d = Demo("unplugged1")
        s1, s2 = d.sample(now=5.0)
        self.assertIsNone(s1)
        self.assertIsNotNone(s2)
        d.scenario = "unplugged2"
        s1, s2 = d.sample(now=5.0)
        self.assertIsNotNone(s1)
        self.assertIsNone(s2)

    def test_boxoff_reports_absent(self):
        d = Demo("boxoff")
        self.assertTrue(d.box_absent())
        self.assertFalse(Demo("normal").box_absent())

    def test_high_climbs_past_graph_top(self):
        d = Demo("high")
        start = d.sample(now=0.0)[0]
        later = d.sample(now=120.0)[0]
        self.assertGreater(later, start)
        self.assertGreater(later, 50.0)
        self.assertLessEqual(d.sample(now=600.0)[0], 63.0)

    def test_low_sinks_under_graph_bottom(self):
        d = Demo("low")
        d.sample(now=0.0)
        later = d.sample(now=120.0)[0]
        self.assertLess(later, 10.0)
        self.assertGreaterEqual(d.sample(now=600.0)[0], -10.0)

    def test_unknown_scenario_rejected(self):
        with self.assertRaises(ValueError):
            Demo("volcano")
        self.assertIn("normal", SCENARIOS)


if __name__ == "__main__":
    unittest.main()
