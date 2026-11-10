from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from audit_sdk import consumer_test_count


class ConsumerEvidenceTests(unittest.TestCase):
    def test_localized_ctest_log_keeps_ascii_test_results(self):
        parent = ROOT / 'build/test-tools'
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as directory:
            log = Path(directory) / 'LastTest.log'
            log.write_bytes('Start testing: 周二 十一月 03\r\n'.encode('gbk') + b'Test Passed.\r\n' * 4)
            self.assertEqual(consumer_test_count(log), 4)
            log.write_bytes(log.read_bytes().replace(b'Test Passed.', b'Test Failed.', 1))
            self.assertEqual(consumer_test_count(log), 3)
