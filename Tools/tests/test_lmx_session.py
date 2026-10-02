"""Checks proposal sidecars against the scene document pair on disk."""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from urllib.parse import unquote


REPOSITORY = Path(__file__).resolve().parents[2]
CLIENT = REPOSITORY / "Tools/Session/lmx_session.py"
TEMPORAL_LAB_LOADED_PAIR_HASH = "4eda44f54b3000353e2cb783bb49892af67760f878b7204b130500e3ef884114"


class SidecarTests(unittest.TestCase):
    def run_client(self, *args):
        return subprocess.run([sys.executable, str(CLIENT), *map(str, args)],
                              text=True, capture_output=True, check=False)

    def test_sidecar_hashes_raw_bytes_and_rebases_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            document = root / "scene with spaces.scene.gltf"
            buffer = root / "motion data.bin"
            buffer.write_bytes(b"\x00\xff\x07\n")
            gltf = b'{\n  "buffers": [{"uri": "motion%20data.bin", "byteLength": 4}]\n}\n'
            document.write_bytes(gltf)
            evidence = root / "captures/frame.png"
            evidence.parent.mkdir()
            evidence.write_bytes(b"frame")

            result = self.run_client("sidecar", document, "--actor", "Lighting client",
                                     "--summary", "Moved lamp", "--evidence", evidence)

            self.assertEqual(result.returncode, 0, result.stderr)
            sidecar = root / "scene with spaces.scene.proposal.json"
            self.assertTrue(sidecar.is_file())
            self.assertEqual(json.loads(sidecar.read_text()), {
                "schema": 1,
                "actor": "Lighting client",
                "summary": "Moved lamp",
                "evidence": ["captures/frame.png"],
                "documentSha256": hashlib.sha256(gltf + buffer.read_bytes()).hexdigest(),
            })

    def test_temporal_lab_pair_uses_buffer_uri(self):
        document = REPOSITORY / "Assets/Scenes/temporal-lab.scene.gltf"
        gltf = document.read_bytes()
        uri = json.loads(gltf)["buffers"][0]["uri"]
        buffer = document.parent / unquote(uri)
        expected = hashlib.sha256(gltf + buffer.read_bytes()).hexdigest()
        self.assertEqual(expected, TEMPORAL_LAB_LOADED_PAIR_HASH)
        with tempfile.TemporaryDirectory() as temp:
            copy = Path(temp) / document.name
            copy.write_bytes(gltf)
            (copy.parent / buffer.name).write_bytes(buffer.read_bytes())
            result = self.run_client("sidecar", copy, "--actor", "Scene client",
                                     "--summary", "Check temporal document")
            self.assertEqual(result.returncode, 0, result.stderr)
            sidecar = copy.with_name("temporal-lab.scene.proposal.json")
            self.assertEqual(json.loads(sidecar.read_text())["documentSha256"],
                             TEMPORAL_LAB_LOADED_PAIR_HASH)

    def test_malformed_or_missing_pair_never_writes_sidecar(self):
        cases = (
            (b"{bad json", None, "JSON"),
            (b"[]", None, "object"),
            (b'{"buffers": [{}]}', None, "uri"),
            (b'{"buffers": [{"uri": "missing.bin", "byteLength": 2}]}', None, "missing.bin"),
            (b'{"buffers": [{"uri": "motion.bin", "byteLength": 3}]}', b"ab", "byteLength"),
            (b'{"buffers": [{"uri": "../motion.bin", "byteLength": 2}]}', b"ab", "URI"),
        )
        for gltf, binary, expected_error in cases:
            with self.subTest(gltf=gltf), tempfile.TemporaryDirectory() as temp:
                document = Path(temp) / "sample.scene.gltf"
                document.write_bytes(gltf)
                if binary is not None:
                    (document.parent / "motion.bin").write_bytes(binary)
                result = self.run_client("sidecar", document, "--actor", "Client",
                                         "--summary", "Summary")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)
                self.assertFalse((document.parent / "sample.scene.proposal.json").exists())

        with tempfile.TemporaryDirectory() as temp:
            document = Path(temp) / "absent.scene.gltf"
            result = self.run_client("sidecar", document, "--actor", "Client",
                                     "--summary", "Summary")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((document.parent / "absent.scene.proposal.json").exists())

    def test_json_forms_incompatible_with_editor_never_write_sidecar(self):
        cases = (
            ('{"note": 1}'.encode("utf-16"), "UTF-8"),
            (b'{"bad": NaN}', "JSON"),
            (b'{"bad": Infinity}', "JSON"),
            (b'{"buffers": [], "buffers": [{"uri": "motion.bin", "byteLength": 2}]}',
             "duplicate"),
            (b'{"buffers": [{"uri": "first.bin", "uri": "motion.bin", "byteLength": 2}]}',
             "duplicate"),
            (b'{"note": "\\ud800"}', "surrogate"),
            (b'{"\\ud800": "note"}', "surrogate"),
        )
        for gltf, expected_error in cases:
            with self.subTest(gltf=gltf), tempfile.TemporaryDirectory() as temp:
                document = Path(temp) / "sample.scene.gltf"
                document.write_bytes(gltf)
                (document.parent / "motion.bin").write_bytes(b"ab")
                result = self.run_client("sidecar", document, "--actor", "Client",
                                         "--summary", "Summary")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)
                self.assertFalse((document.parent / "sample.scene.proposal.json").exists())

    def test_selftest(self):
        result = self.run_client("--selftest")
        self.assertEqual(result.returncode, 0, result.stderr)


class BridgeClientTests(unittest.TestCase):
    def run_client(self, *args, env=None):
        return subprocess.run([sys.executable, str(CLIENT), *map(str, args)],
                              text=True, capture_output=True, check=False, env=env, timeout=8)

    def serve(self, path, response=None, stall=False):
        ready = threading.Event()
        requests = []

        def worker():
            with socket.socket(socket.AF_UNIX) as server:
                server.bind(str(path))
                server.listen(1)
                ready.set()
                client, _ = server.accept()
                with client, client.makefile("rb") as stream:
                    for index in range(2):
                        request = json.loads(stream.readline())
                        requests.append(request)
                        if stall and index == 1:
                            time.sleep(0.3)
                            return
                        payload = ({"id": request["id"], "ok": True,
                                    "result": {"protocol": 1} if index == 0 else {"value": 7}}
                                   if response is None or index == 0 else
                                   {"id": request["id"], "ok": False,
                                    "error": {"code": "tier", "message": "ReadOnly"}})
                        client.sendall((json.dumps(payload) + "\n").encode())

        thread = threading.Thread(target=worker, daemon=True)
        thread.start()
        self.assertTrue(ready.wait(2))
        return thread, requests

    def test_framing_and_json_success(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "session.sock"
            thread, requests = self.serve(path)
            result = self.run_client("--socket", path, "--name", "Test client", "query", "console",
                                     "--after-sequence", "17")
            thread.join(2)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["result"], {"value": 7})
            self.assertEqual(requests, [
                {"id": 1, "command": "hello", "args": {"name": "Test client", "protocol": 1}},
                {"id": 2, "command": "query.console", "args": {"afterSequence": 17}},
            ])

    def test_error_response_and_transport_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "session.sock"
            thread, _ = self.serve(path, response="tier")
            refused = self.run_client("--socket", path, "propose", "withdraw", "12")
            thread.join(2)
            self.assertEqual(refused.returncode, 2, refused.stderr)
            self.assertEqual(json.loads(refused.stdout)["error"]["code"], "tier")
            missing = self.run_client("--socket", path, "query", "status")
            self.assertEqual(missing.returncode, 3)
            self.assertEqual(json.loads(missing.stdout)["error"]["code"], "transport")

    def test_timeout(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "session.sock"
            thread, _ = self.serve(path, stall=True)
            result = self.run_client("--socket", path, "--timeout", "0.05", "query", "status")
            thread.join(2)
            self.assertEqual(result.returncode, 3)
            self.assertEqual(json.loads(result.stdout)["error"]["code"], "transport")

    def test_wait_tier_polls_same_connection_until_operator_raises_ceiling(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "session.sock"
            ready = threading.Event()
            requests = []

            def worker():
                with socket.socket(socket.AF_UNIX) as server:
                    server.bind(str(path))
                    server.listen(1)
                    ready.set()
                    client, _ = server.accept()
                    with client, client.makefile("rb") as stream:
                        hello = json.loads(stream.readline())
                        requests.append(hello)
                        client.sendall((json.dumps({"id": hello["id"], "ok": True,
                                                    "result": {"protocol": 1}}) + "\n").encode())
                        for ceiling in (0, 0, 2):
                            request = json.loads(stream.readline())
                            requests.append(request)
                            client.sendall((json.dumps({"id": request["id"], "ok": True,
                                                        "result": {"tier": ceiling}}) + "\n").encode())
                        command = json.loads(stream.readline())
                        requests.append(command)
                        client.sendall((json.dumps({"id": command["id"], "ok": True,
                                                    "result": {"applied": True}}) + "\n").encode())

            thread = threading.Thread(target=worker, daemon=True)
            thread.start()
            self.assertTrue(ready.wait(2))
            result = self.run_client("--socket", path, "--wait-tier", "--timeout", "2",
                                     "settings", "set", "temporal", "off")
            thread.join(2)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["result"], {"applied": True})
            self.assertEqual([request["command"] for request in requests],
                             ["hello", "query.status", "query.status", "query.status",
                              "settings.set"])
            self.assertEqual(requests[-1]["args"], {"temporal": "off"})

    def test_wait_tier_times_out_without_sending_apply_command(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "session.sock"
            ready = threading.Event()
            requests = []

            def worker():
                with socket.socket(socket.AF_UNIX) as server:
                    server.bind(str(path))
                    server.listen(1)
                    ready.set()
                    client, _ = server.accept()
                    with client, client.makefile("rb") as stream:
                        while line := stream.readline():
                            request = json.loads(line)
                            requests.append(request)
                            result = {"protocol": 1} if request["command"] == "hello" else {"tier": 0}
                            client.sendall((json.dumps({"id": request["id"], "ok": True,
                                                        "result": result}) + "\n").encode())

            thread = threading.Thread(target=worker, daemon=True)
            thread.start()
            self.assertTrue(ready.wait(2))
            result = self.run_client("--socket", path, "--wait-tier", "--timeout", "0.35",
                                     "propose", "withdraw", "1")
            thread.join(2)
            self.assertEqual(result.returncode, 3)
            self.assertEqual(json.loads(result.stdout)["error"]["code"], "transport")
            self.assertTrue(all(request["command"] in ("hello", "query.status")
                                for request in requests))

    def test_client_name_is_not_overwritten_by_setting_name(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "session.sock"
            thread, requests = self.serve(path)
            result = self.run_client("--socket", path, "--name", "Lighting tool",
                                     "settings", "set", "temporal", "off")
            thread.join(2)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(requests[0]["args"]["name"], "Lighting tool")

    def test_slow_response_chunks_cannot_extend_whole_request_timeout(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "session.sock"
            ready = threading.Event()

            def worker():
                with socket.socket(socket.AF_UNIX) as server:
                    server.bind(str(path))
                    server.listen(1)
                    ready.set()
                    client, _ = server.accept()
                    with client, client.makefile("rb") as stream:
                        hello = json.loads(stream.readline())
                        client.sendall((json.dumps({"id": hello["id"], "ok": True,
                                                    "result": {"protocol": 1}}) + "\n").encode())
                        request = json.loads(stream.readline())
                        line = (json.dumps({"id": request["id"], "ok": True,
                                            "result": {"value": 7}}) + "\n").encode()
                        for byte in line:
                            try:
                                client.sendall(bytes((byte,)))
                            except BrokenPipeError:
                                break
                            time.sleep(0.02)

            thread = threading.Thread(target=worker, daemon=True)
            thread.start()
            self.assertTrue(ready.wait(2))
            started = time.monotonic()
            result = self.run_client("--socket", path, "--timeout", "0.15", "query", "status")
            elapsed = time.monotonic() - started
            thread.join(2)
            self.assertEqual(result.returncode, 3)
            self.assertLess(elapsed, 0.65)

    def test_discovery_skips_newer_stale_socket_with_live_pid(self):
        with tempfile.TemporaryDirectory() as temp:
            live = Path(temp) / f"luminex-session-{os.getpid()}.sock"
            thread, _ = self.serve(live)
            sleeper = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(5)"])
            try:
                stale = Path(temp) / f"luminex-session-{sleeper.pid}.sock"
                with socket.socket(socket.AF_UNIX) as closed:
                    closed.bind(str(stale))
                future = time.time() + 10
                os.utime(stale, (future, future))
                env = dict(os.environ, TMPDIR=temp)
                env.pop("LMX_SESSION_SOCKET", None)
                result = self.run_client("query", "status", env=env)
                thread.join(2)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(json.loads(result.stdout)["result"], {"value": 7})
            finally:
                sleeper.terminate()
                sleeper.wait(timeout=2)

    def test_hello_error_response_is_reported_as_protocol_error(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "session.sock"
            ready = threading.Event()

            def worker():
                with socket.socket(socket.AF_UNIX) as server:
                    server.bind(str(path))
                    server.listen(1)
                    ready.set()
                    client, _ = server.accept()
                    with client, client.makefile("rb") as stream:
                        request = json.loads(stream.readline())
                        client.sendall((json.dumps({"id": request["id"], "ok": False,
                                                    "error": {"code": "protocol",
                                                              "message": "Wrong version"}}) + "\n").encode())

            thread = threading.Thread(target=worker, daemon=True)
            thread.start()
            self.assertTrue(ready.wait(2))
            result = self.run_client("--socket", path, "query", "status")
            thread.join(2)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(json.loads(result.stdout), {"id": 1, "ok": False,
                                                         "error": {"code": "protocol",
                                                                   "message": "Wrong version"}})

    def test_environment_and_discovery_skip_dead_pid(self):
        with tempfile.TemporaryDirectory() as temp:
            live = Path(temp) / f"luminex-session-{os.getpid()}.sock"
            dead = Path(temp) / "luminex-session-99999999.sock"
            dead.touch()
            os.utime(dead, (time.time() + 10, time.time() + 10))
            thread, _ = self.serve(live)
            env = dict(os.environ, TMPDIR=temp)
            env.pop("LMX_SESSION_SOCKET", None)
            discovered = self.run_client("query", "status", env=env)
            thread.join(2)
            self.assertEqual(discovered.returncode, 0, discovered.stderr)
            self.assertTrue(stat.S_ISSOCK(live.stat().st_mode))
            live.unlink()
            thread, _ = self.serve(live)
            env["LMX_SESSION_SOCKET"] = str(live)
            configured = self.run_client("query", "status", env=env)
            thread.join(2)
            self.assertEqual(configured.returncode, 0, configured.stderr)

    def test_every_bridge_command_maps_to_wire_name(self):
        samples = {
            "query.status": ("query", "status"),
            "query.hierarchy": ("query", "hierarchy"),
            "query.selection": ("query", "selection"),
            "query.camera": ("query", "camera"),
            "query.settings": ("query", "settings"),
            "query.readings": ("query", "readings"),
            "query.performance": ("query", "performance"),
            "query.graph": ("query", "graph"),
            "query.console": ("query", "console"),
            "query.proposals": ("query", "proposals"),
            "query.log": ("query", "log"),
            "propose.edits": ("propose", "edits", "--summary", "Move", "--edit", "object:0",
                              "position", "[1,2,3]"),
            "propose.withdraw": ("propose", "withdraw", "1"),
            "settings.set": ("settings", "set", "temporal", "off"),
            "debugview.set": ("debugview", "set", "final"),
            "scene.open": ("scene", "open", "sponza"),
            "measure.run": ("measure", "run", "0", "1", "measure.json"),
            "capture.gpu": ("capture", "gpu"),
            "capture.screenshot": ("capture", "screenshot", "frame", "1"),
            "capture.sequence": ("capture", "sequence", "frames", "1", "0"),
            "graph.dump": ("graph", "dump", "graph.txt"),
            "plan.submit": ("plan", "submit"),
        }
        for command, words in samples.items():
            with self.subTest(command=command), tempfile.TemporaryDirectory() as temp:
                path = Path(temp) / "session.sock"
                thread, requests = self.serve(path)
                if command == "plan.submit":
                    plan = Path(temp) / "plan.json"
                    plan.write_text('{"summary":"Check","steps":[{"command":"settings.set",'
                                    '"args":{"temporal":"off"}}]}')
                    words = (*words, str(plan))
                result = self.run_client("--socket", path, *words)
                thread.join(2)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(requests[1]["command"], command)
                self.assertIsInstance(requests[1]["args"], dict)
    def serve_busy(self, path):
        ready = threading.Event()

        def worker():
            with socket.socket(socket.AF_UNIX) as server:
                server.bind(str(path))
                server.listen(1)
                ready.set()
                client, _ = server.accept()
                with client:
                    client.sendall((json.dumps({"id": 0, "ok": False, "error": {
                        "code": "busy", "message": "A session client is connected"}}) + "\n").encode())
                    time.sleep(0.3)

        thread = threading.Thread(target=worker, daemon=True)
        thread.start()
        self.assertTrue(ready.wait(2))
        return thread

    def test_busy_response_with_id_zero_is_a_server_error(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "session.sock"
            thread = self.serve_busy(path)
            result = self.run_client("--socket", path, "query", "status")
            thread.join(2)
            self.assertEqual(result.returncode, 2, result.stdout)
            self.assertEqual(json.loads(result.stdout), {"id": 0, "ok": False, "error": {
                "code": "busy", "message": "A session client is connected"}})

    def test_id_zero_without_error_object_is_still_a_transport_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "session.sock"
            ready = threading.Event()

            def worker():
                with socket.socket(socket.AF_UNIX) as server:
                    server.bind(str(path))
                    server.listen(1)
                    ready.set()
                    client, _ = server.accept()
                    with client:
                        client.sendall(b'{"id":0,"ok":false}\n')
                        time.sleep(0.3)

            thread = threading.Thread(target=worker, daemon=True)
            thread.start()
            self.assertTrue(ready.wait(2))
            result = self.run_client("--socket", path, "query", "status")
            thread.join(2)
            self.assertEqual(result.returncode, 3)

    def test_discovery_moves_past_busy_editor_to_next_candidate(self):
        with tempfile.TemporaryDirectory() as temp:
            busy = Path(temp) / f"luminex-session-{os.getpid()}.sock"
            sleeper = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(5)"])
            try:
                free = Path(temp) / f"luminex-session-{sleeper.pid}.sock"
                thread, _ = self.serve(free)
                busy_thread = self.serve_busy(busy)
                future = time.time() + 10
                os.utime(busy, (future, future))
                env = dict(os.environ, TMPDIR=temp)
                env.pop("LMX_SESSION_SOCKET", None)
                result = self.run_client("query", "status", env=env)
                thread.join(2)
                busy_thread.join(2)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertEqual(json.loads(result.stdout)["result"], {"value": 7})
            finally:
                sleeper.terminate()
                sleeper.wait(timeout=2)

    def test_invalid_timeout_is_invalid_input_before_connecting(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "never.sock"
            for value in ("0", "-1", "abc", "nan"):
                result = self.run_client("--socket", path, "--timeout", value, "query", "status")
                self.assertEqual(result.returncode, 2, (value, result.stdout, result.stderr))
                self.assertEqual(json.loads(result.stdout)["error"]["code"], "invalid")

    def test_unset_tmpdir_falls_back_to_slash_tmp(self):
        spec = importlib.util.spec_from_file_location("lmx_session_under_test", CLIENT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as other:
            saved = {key: os.environ.get(key) for key in ("TMPDIR", "TEMP", "TMP")}
            try:
                os.environ.pop("TMPDIR", None)
                os.environ["TEMP"] = os.environ["TMP"] = other
                with self.assertRaises(module.TransportError) as caught:
                    module.discover_sockets()
                self.assertIn("in /tmp", str(caught.exception))
            finally:
                for key, value in saved.items():
                    if value is None:
                        os.environ.pop(key, None)
                    else:
                        os.environ[key] = value


if __name__ == "__main__":
    unittest.main()
