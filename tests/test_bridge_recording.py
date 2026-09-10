"""Exercise real HTTP framing and WAV commit/abort behavior."""
import importlib.util
import socket
import tempfile
import threading
import unittest
import wave
from pathlib import Path
from http.server import ThreadingHTTPServer

spec = importlib.util.spec_from_file_location('bridge', Path(__file__).resolve().parents[1] / 'bridge/server.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class RecordingTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        bridge.ROOT = Path(self.tmp.name)
        bridge.FEEDBACK_DIR = bridge.ROOT / 'feedback'
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), bridge.BridgeHandler)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.tmp.cleanup()

    def send(self, body, path='/feedback?task_id=test&hz=16000&bits=16&ch=1', headers='Transfer-Encoding: chunked\r\n'):
        with socket.create_connection(self.server.server_address, timeout=3) as sock:
            sock.sendall(f'POST {path} HTTP/1.1\r\nHost: localhost\r\n{headers}\r\n'.encode() + body)
            sock.shutdown(socket.SHUT_WR)
            result = b''
            while data := sock.recv(4096):
                result += data
            return int(result.split(b' ')[1])

    def test_complete_recording(self):
        pcm = bytes(range(256)) * 2
        self.assertEqual(self.send((b'200\r\n' + pcm + b'\r\n') * 400 + b'0\r\n\r\n'), 201)
        files = list(bridge.ROOT.rglob('*.wav'))
        self.assertEqual(len(files), 1)
        with wave.open(str(files[0])) as wav:
            self.assertEqual(wav.getframerate(), 16000)
            self.assertEqual(wav.readframes(wav.getnframes()), pcm * 400)

    def test_invalid_or_cancelled_never_commits(self):
        for body in [b'', b'0\r\n\r\n', b'2\r\na', b'2\r\nab\r\n',
                     b'2\r\nab\r\n0\r\n', b'201\r\n', b'3\r\nabc\r\n0\r\n\r\n',
                     b'garbage\r\n', b'2\r\nabXX0\r\n\r\n']:
            with self.subTest(body=body):
                self.assertEqual(self.send(body), 400)
                self.assertEqual(list(bridge.ROOT.rglob('*.wav')), [])
                self.assertEqual(list(bridge.ROOT.rglob('*.part')), [])

    def test_duration_limit_and_unique_files(self):
        chunk = b'200\r\n' + bytes(512) + b'\r\n'
        self.assertEqual(self.send(chunk * 1875 + b'0\r\n\r\n'), 201)
        self.assertEqual(self.send(b'2\r\nab\r\n0\r\n\r\n'), 201)
        self.assertEqual(len(list(bridge.ROOT.rglob('*.wav'))), 2)
        self.assertEqual(self.send(chunk * 1875 + b'2\r\nab\r\n0\r\n\r\n'), 400)
        self.assertEqual(len(list(bridge.ROOT.rglob('*.wav'))), 2)
        self.assertEqual(list(bridge.ROOT.rglob('*.part')), [])

    def test_limits_and_metadata(self):
        self.assertEqual(self.send(b'', path='/other'), 404)
        self.assertEqual(self.send(b'', path='/feedback?task_id=..&hz=16000&bits=16&ch=1'), 400)
        self.assertEqual(self.send(b'', headers='Content-Length: 2\r\n'), 400)
        self.assertEqual(self.send(b'', path='/feedback?hz=oops'), 400)


if __name__ == '__main__':
    unittest.main()
