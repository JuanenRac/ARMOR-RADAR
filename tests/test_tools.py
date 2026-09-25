"""Tests of ARMOR-RADAR's bench tools (python -m unittest discover tests)."""
import importlib.util
import pathlib
import unittest

TOOL = pathlib.Path(__file__).resolve().parent.parent / "tools" / "frames_to_fixture.py"
spec = importlib.util.spec_from_file_location("frames_to_fixture", TOOL)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)

def frame(payload_hex):
    body = ("aaff0300" + payload_hex).ljust(56, "0")
    return body + "55cc"


class FramesToFixture(unittest.TestCase):
    def test_keeps_complete_frames_and_counts_the_cut_ones(self):
        complete = frame("0e03b1a86400" + "4001")
        lines = [
            f"I (1234) armor-radar: FRAME r1 {complete}",
            "I (1300) armor-radar: radar 1: reporting",
            f"I (1400) armor-radar: FRAME r3 {complete}",
            "I (1500) armor-radar: FRAME r2 aaff03000e03",  # a line cut in the middle
            f"I (1600) armor-radar: FRAME r4 {complete}",  # there is no radar 4
        ]
        good, skipped = tool.convert(lines)
        self.assertEqual([entry[:2] for entry in good], ["r1", "r3"])
        self.assertEqual(skipped, 1)
        self.assertTrue(all(len(entry.split()[1]) == 60 for entry in good))

    def test_a_frame_without_the_manuals_header_or_tail_is_refused(self):
        wrong_header = "bbff0300" + "00" * 22 + "55cc"
        wrong_tail = "aaff0300" + "00" * 22 + "55cd"
        good, skipped = tool.convert([f"FRAME r1 {wrong_header}", f"FRAME r1 {wrong_tail}"])
        self.assertEqual((good, skipped), ([], 2))


if __name__ == "__main__":
    unittest.main()
