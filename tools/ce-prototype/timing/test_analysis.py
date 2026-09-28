import unittest
from analyze import containing_frame, summary

class IdentityTests(unittest.TestCase):
    def test_present_must_contain_event_not_just_be_nearest(self):
        rows = [{"id":"1", "end_qpc":"20"}, {"id":"2", "end_qpc":"40"}]
        self.assertIsNone(containing_frame([10,30], rows, 25))
        self.assertIsNone(containing_frame([10,30], rows, 9))
        self.assertEqual(containing_frame([10,30], rows, 31), 2)
        self.assertEqual(containing_frame([10,30], rows, 20), 1)

    def test_signed_observations_are_not_clamped(self):
        self.assertEqual(summary([-1, 1])["mean_ms"], 0)
        self.assertEqual(summary([-1, 1])["min_ms"], -1)
        self.assertEqual(summary([]), {"n":0})

if __name__ == '__main__':
    unittest.main()
