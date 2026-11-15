import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("karaoke_align", Path(__file__).with_name("align.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Result:
    def __init__(self, words):
        self.words = words

    def to_dict(self):
        return {"segments": [{"words": self.words}]}


class AlignmentTests(unittest.TestCase):
    def test_pause_preserved(self):
        result = Result([{"word": "Hello", "start": 1., "end": 1.4}, {"word": " world", "start": 2., "end": 2.5}])
        self.assertIn("<00:01.400><00:02.000>", module.render(result, ["Hello world"]))

    def test_wrong_transcript_rejected(self):
        with self.assertRaises(ValueError):
            module.render(Result([{"word": "Wrong", "start": 0., "end": 1.}]), ["Correct"])

    def test_overlap_rejected(self):
        with self.assertRaises(ValueError):
            module.render(Result([{"word": "a", "start": 1., "end": 3.}, {"word": " b", "start": 2., "end": 4.}]), ["a b"])

    def test_invalid_time(self):
        for value in [float("nan"), float("inf"), -1.]:
            with self.assertRaises(ValueError):
                module.timestamp(value)


if __name__ == "__main__":
    unittest.main()
