import hashlib
import http.server
from pathlib import Path
import shutil
import socketserver
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from dependency_support import download_ranges


@unittest.skipUnless(shutil.which("curl"), "curl is the resumable downloader prerequisite")
class RangeDownloadTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / "build/tests/download-ranges"
        directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=str(directory))
        self.addCleanup(self.temporary.cleanup)

    def serve(self, handler):
        server = socketserver.TCPServer(("127.0.0.1", 0), handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        return "http://127.0.0.1:{}/source".format(server.server_address[1])

    def test_truncated_transfer_resumes_and_matches_original_bytes(self):
        payload = bytes(range(256)) * 64
        requests = []

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                start, end = map(int, self.headers["Range"][len("bytes="):].split("-"))
                requests.append((start, end))
                self.send_response(206)
                self.send_header("Content-Range", "bytes {}-{}/{}".format(start, end, len(payload)))
                self.send_header("Content-Length", str(end - start + 1))
                self.end_headers()
                self.wfile.write(payload[start:start + 173] if len(requests) == 1 else payload[start:end + 1])

            def log_message(self, *_):
                pass

        url = self.serve(Handler)
        result = download_ranges({"archive": "sample", "bytes": len(payload)}, url, Path(self.temporary.name))
        self.assertEqual(result.read_bytes(), payload)
        self.assertEqual(requests, [(0, len(payload) - 1), (173, len(payload) - 1)])

    def test_server_ignoring_ranges_cannot_supply_chunk_bytes(self):
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"whole-file-response")

            def log_message(self, *_):
                pass

        url = self.serve(Handler)
        with self.assertRaisesRegex(RuntimeError, "did not honor"):
            download_ranges({"archive": "sample", "bytes": 19}, url, Path(self.temporary.name))
        self.assertFalse((Path(self.temporary.name) / "complete.download").exists())


if __name__ == "__main__":
    unittest.main()
