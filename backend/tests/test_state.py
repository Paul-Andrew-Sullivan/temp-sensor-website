import unittest

from state import ProbeState, HISTORY_LEN


class FreshState(unittest.TestCase):
    def test_box_is_off_and_history_empty(self):
        st = ProbeState()
        s = st.state(now=1000.0)
        self.assertEqual(s["box"], "off")
        self.assertIsNone(s["s1"]["temp"])
        self.assertIsNone(s["s2"]["temp"])
        h = st.history(now=1000.0)
        self.assertEqual(len(h["s1"]), HISTORY_LEN)
        self.assertEqual(len(h["s2"]), HISTORY_LEN)
        self.assertTrue(all(v is None for v in h["s1"]))


class Ingest(unittest.TestCase):
    def test_ingest_shows_temps_and_box_on(self):
        st = ProbeState()
        st.ingest(21.4, 19.9, True, False, now=1000.0)
        s = st.state(now=1000.5)
        self.assertEqual(s["box"], "on")
        self.assertEqual(s["s1"]["temp"], 21.4)
        self.assertEqual(s["s2"]["temp"], 19.9)
        self.assertTrue(s["s1"]["plugged"])
        self.assertTrue(s["s1"]["on"])
        self.assertFalse(s["s2"]["on"])

    def test_stale_ingest_reads_box_off(self):
        st = ProbeState(box_timeout=3.0)
        st.ingest(21.4, 19.9, True, True, now=1000.0)
        self.assertEqual(st.state(now=1002.9)["box"], "on")
        s = st.state(now=1003.1)
        self.assertEqual(s["box"], "off")
        self.assertIsNone(s["s1"]["temp"])

    def test_sensor_error_values_read_unplugged(self):
        st = ProbeState()
        st.ingest(-127.0, 85.0, True, True, now=1000.0)
        s = st.state(now=1000.0)
        self.assertFalse(s["s1"]["plugged"])
        self.assertIsNone(s["s1"]["temp"])
        self.assertFalse(s["s2"]["plugged"])

    def test_none_reads_unplugged(self):
        st = ProbeState()
        st.ingest(None, 20.0, True, True, now=1000.0)
        s = st.state(now=1000.0)
        self.assertFalse(s["s1"]["plugged"])
        self.assertTrue(s["s2"]["plugged"])


class History(unittest.TestCase):
    def test_tick_appends_newest_last(self):
        st = ProbeState()
        st.ingest(20.0, 30.0, True, True, now=1000.0)
        st.tick(now=1000.0)
        st.ingest(21.0, 31.0, True, True, now=1001.0)
        st.tick(now=1001.0)
        h = st.history(now=1001.0)
        self.assertEqual(h["s1"][-1], 21.0)
        self.assertEqual(h["s1"][-2], 20.0)
        self.assertEqual(h["s2"][-1], 31.0)
        self.assertEqual(len(h["s1"]), HISTORY_LEN)

    def test_tick_when_box_off_appends_none(self):
        st = ProbeState(box_timeout=3.0)
        st.ingest(20.0, 30.0, True, True, now=1000.0)
        st.tick(now=1000.0)
        st.tick(now=1010.0)
        h = st.history(now=1010.0)
        self.assertIsNone(h["s1"][-1])
        self.assertEqual(h["s1"][-2], 20.0)

    def test_ring_never_exceeds_len(self):
        st = ProbeState()
        for i in range(HISTORY_LEN + 50):
            st.ingest(float(i), 0.0, True, True, now=1000.0 + i)
            st.tick(now=1000.0 + i)
        h = st.history(now=2000.0)
        self.assertEqual(len(h["s1"]), HISTORY_LEN)
        self.assertEqual(h["s1"][-1], float(HISTORY_LEN + 49))
        self.assertEqual(h["s1"][0], 50.0)


class Buttons(unittest.TestCase):
    def test_press_sets_wanted_and_ingest_echoes(self):
        st = ProbeState()
        st.ingest(20.0, 30.0, True, True, now=1000.0)
        r = st.press(2, False)
        self.assertEqual(r, {"b1": True, "b2": False})
        reply = st.ingest(20.0, 30.0, True, True, now=1000.5)
        self.assertEqual(reply, {"b1": True, "b2": False})

    def test_state_reports_wanted_until_board_confirms(self):
        st = ProbeState()
        st.ingest(20.0, 30.0, True, True, now=1000.0)
        st.press(1, False)
        self.assertFalse(st.state(now=1000.1)["s1"]["on"])

    def test_press_rejects_bad_sensor(self):
        st = ProbeState()
        with self.assertRaises(ValueError):
            st.press(3, True)


if __name__ == "__main__":
    unittest.main()
