#!/usr/bin/env python3
"""Write proposal sidecars and send commands to a local Luminex session."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import stat
import sys
import tempfile
import threading
import time
from urllib.parse import unquote_to_bytes


_URI_CHARACTERS = re.compile(r"[A-Za-z0-9._~!$&'()*+,;=@/-]*\Z")
_HEX_ESCAPE = re.compile(r"%[0-9A-Fa-f]{2}")
_MAX_LINE_BYTES = 1 << 20
_QUERIES = ("status", "hierarchy", "selection", "camera", "settings", "readings",
            "performance", "graph", "console", "proposals", "log")


class TransportError(Exception):
    """A socket connection or protocol response failed."""


def discover_sockets() -> list[Path]:
    """List live-PID editor sockets newest first; connect before trusting a candidate."""
    directory = Path(os.environ.get("TMPDIR") or tempfile.gettempdir())
    candidates = []
    for path in directory.glob("luminex-session-*.sock"):
        match = re.fullmatch(r"luminex-session-(\d+)\.sock", path.name)
        if not match:
            continue
        try:
            status = path.stat()
            if not stat.S_ISSOCK(status.st_mode):
                continue
            os.kill(int(match.group(1)), 0)
        except (OSError, ValueError):
            continue
        candidates.append((status.st_mtime_ns, path))
    if not candidates:
        raise TransportError(f"No live Luminex session socket in {directory}")
    return [path for _, path in sorted(candidates, reverse=True)]


def _receive_line(connection: socket.socket, deadline: float) -> dict:
    data = bytearray()
    while len(data) <= _MAX_LINE_BYTES:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TransportError("Session request timed out")
        connection.settimeout(remaining)
        chunk = connection.recv(min(4096, _MAX_LINE_BYTES + 1 - len(data)))
        if not chunk:
            raise TransportError("Session closed before responding")
        data.extend(chunk)
        if b"\n" in chunk:
            line, _, remainder = data.partition(b"\n")
            if remainder or len(line) > _MAX_LINE_BYTES:
                raise TransportError("Invalid response framing")
            try:
                response = json.loads(line.decode("utf-8"), parse_constant=_reject_constant)
            except (UnicodeDecodeError, ValueError) as error:
                raise TransportError(f"Invalid JSON response: {error}") from error
            if not isinstance(response, dict):
                raise TransportError("Response must be a JSON object")
            return response
    raise TransportError("Response exceeds 1 MiB")


def _connect(paths: list[Path], deadline: float) -> socket.socket:
    last_error = None
    for path in paths:
        connection = socket.socket(socket.AF_UNIX)
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                connection.close()
                raise TransportError("Session request timed out")
            connection.settimeout(remaining)
            connection.connect(str(path))
            return connection
        except OSError as error:
            last_error = error
            connection.close()
    raise TransportError(f"Cannot connect to a live session: {last_error}")


def send_command(paths: list[Path], timeout: float, name: str, command: str, args: dict,
                 wait_tier: bool = False) -> dict:
    """Exchange hello and one command; tier polling never changes editor state."""
    if timeout <= 0:
        raise TransportError("Timeout must be positive")
    deadline = time.monotonic() + timeout
    try:
        with _connect(paths, deadline) as connection:
            request_id = 0

            def exchange(wire_command: str, wire_args: dict) -> dict:
                nonlocal request_id
                request_id += 1
                request = {"id": request_id, "command": wire_command, "args": wire_args}
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TransportError("Session request timed out")
                connection.settimeout(remaining)
                line = (json.dumps(request, ensure_ascii=False, separators=(",", ":")) + "\n").encode()
                if len(line) > _MAX_LINE_BYTES:
                    raise TransportError("Request exceeds 1 MiB")
                connection.sendall(line)
                response = _receive_line(connection, deadline)
                if response.get("id") != request["id"] or not isinstance(response.get("ok"), bool):
                    raise TransportError("Response id or shape does not match request")
                return response

            hello = exchange("hello", {"name": name, "protocol": 1})
            if not hello["ok"]:
                return hello
            if not isinstance(hello.get("result"), dict) or hello["result"].get("protocol") != 1:
                raise TransportError("Session protocol 1 was not acknowledged")

            required_tier = (0 if command.startswith("query.") else
                             1 if command.startswith("propose.") else 2)
            if wait_tier and required_tier:
                while True:
                    status = exchange("query.status", {})
                    if not status["ok"]:
                        return status
                    result = status.get("result")
                    tier = result.get("tier") if isinstance(result, dict) else None
                    if type(tier) is not int or tier not in (0, 1, 2):
                        raise TransportError("query.status returned an invalid tier")
                    if tier >= required_tier:
                        break
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TransportError("Session request timed out waiting for operator tier")
                    time.sleep(min(0.1, remaining))
            return exchange(command, args)
    except (OSError, socket.timeout) as error:
        raise TransportError(str(error)) from error


def _reject_constant(value: str):
    raise ValueError(f"Nonstandard JSON constant {value}")


def _buffer_path(document: Path, uri: str) -> Path:
    """Decode the relative external buffer URI accepted by scene documents."""
    if not uri or not isinstance(uri, str):
        raise ValueError("buffer uri must be a nonempty string")
    unescaped = _HEX_ESCAPE.sub("", uri)
    if "%" in unescaped or not _URI_CHARACTERS.fullmatch(unescaped):
        raise ValueError("buffer URI contains an invalid escape or character")
    try:
        decoded = unquote_to_bytes(uri).decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError("buffer URI is not UTF-8") from error
    path = Path(decoded)
    if (not decoded or path.is_absolute() or ".." in path.parts or
            any(character in decoded for character in ("\x00", "\\", ":")) or
            path.suffix != ".bin"):
        raise ValueError("buffer URI must name a relative external .bin without parent traversal")
    return document.parent / path


def document_hash(document: Path) -> str:
    """Match sceneDocumentHash over raw glTF bytes followed by referenced buffer bytes."""
    try:
        gltf = document.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read document '{document}': {error.strerror}") from error
    try:
        text = gltf.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError(f"glTF JSON must be UTF-8: {error}") from error

    def reject_constant(value: str):
        raise ValueError(f"invalid JSON constant {value}")

    def unique_object(pairs: list[tuple[str, object]]) -> dict:
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key {key!r}")
            result[key] = value
        return result

    try:
        parsed = json.loads(text, parse_constant=reject_constant, object_pairs_hook=unique_object)
    except (json.JSONDecodeError, ValueError) as error:
        raise ValueError(f"invalid glTF JSON: {error}") from error

    def validate_strings(value):
        if isinstance(value, str):
            try:
                value.encode("utf-8")
            except UnicodeEncodeError as error:
                raise ValueError("invalid JSON surrogate escape") from error
        elif isinstance(value, dict):
            for key, item in value.items():
                validate_strings(key)
                validate_strings(item)
        elif isinstance(value, list):
            for item in value:
                validate_strings(item)

    validate_strings(parsed)
    if not isinstance(parsed, dict):
        raise ValueError("glTF JSON must be an object")

    buffers = parsed.get("buffers")
    if buffers is None and "buffers" not in parsed:
        return hashlib.sha256(gltf).hexdigest()
    if not isinstance(buffers, list) or len(buffers) != 1 or not isinstance(buffers[0], dict):
        raise ValueError("buffers must contain exactly one external buffer")
    entry = buffers[0]
    if not isinstance(entry.get("uri"), str):
        raise ValueError("buffer uri must be a string")
    buffer = _buffer_path(document, entry["uri"])
    length = entry.get("byteLength")
    if isinstance(length, bool) or not isinstance(length, int) or length <= 0:
        raise ValueError("buffer byteLength must be a positive integer")
    try:
        binary = buffer.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read buffer '{buffer}': {error.strerror}") from error
    if len(binary) != length:
        raise ValueError(f"buffer byteLength is {len(binary)}, expected {length}")
    digest = hashlib.sha256(gltf)
    digest.update(binary)
    return digest.hexdigest()


def sidecar_path(document: Path) -> Path:
    """Use the same name as the Session sidecar reader."""
    suffix = ".scene.gltf"
    if document.name.endswith(suffix):
        return document.with_name(document.name[:-len(suffix)] + ".scene.proposal.json")
    return document.with_name(document.name + ".proposal.json")


def write_sidecar(document: Path, actor: str, summary: str, evidence: list[Path]) -> Path:
    """Validate the pair, then atomically publish its sidecar beside the glTF."""
    destination = sidecar_path(document)
    digest = document_hash(document)
    relative_evidence = [os.path.relpath(path.resolve(), destination.parent.resolve())
                         for path in evidence]
    payload = {
        "schema": 1,
        "actor": actor,
        "summary": summary,
        "evidence": relative_evidence,
        "documentSha256": digest,
    }
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=destination.parent,
                                         prefix=".lmx-sidecar-", suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            json.dump(payload, output, ensure_ascii=False, indent=2)
            output.write("\n")
        os.replace(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return destination


def selftest() -> None:
    """Exercise sidecars and the bridge wire protocol with a local fake server."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        document = root / "check.scene.gltf"
        gltf = b'{"buffers":[{"uri":"other.bin","byteLength":2}]}'
        binary = b"\x00\xff"
        document.write_bytes(gltf)
        (root / "other.bin").write_bytes(binary)
        sidecar = write_sidecar(document, "Selftest", "Check pair", [])
        expected = hashlib.sha256(gltf + binary).hexdigest()
        assert json.loads(sidecar.read_text(encoding="utf-8"))["documentSha256"] == expected
        socket_path = root / "selftest.sock"
        ready = threading.Event()
        received = []

        def fake_server():
            with socket.socket(socket.AF_UNIX) as server:
                server.bind(str(socket_path))
                server.listen(1)
                ready.set()
                connection, _ = server.accept()
                with connection, connection.makefile("rb") as stream:
                    for index in range(2):
                        request = json.loads(stream.readline())
                        received.append(request)
                        result = {"protocol": 1} if index == 0 else {"tier": 0}
                        connection.sendall((json.dumps({"id": request["id"], "ok": True,
                                                        "result": result}) + "\n").encode())

        thread = threading.Thread(target=fake_server, daemon=True)
        thread.start()
        assert ready.wait(2)
        response = send_command([socket_path], 2, "Selftest", "query.status", {})
        thread.join(2)
        assert not thread.is_alive()
        assert [request["command"] for request in received] == ["hello", "query.status"]
        assert response["result"] == {"tier": 0}
        print("sidecar and bridge selftest passed")


def _arguments(options: argparse.Namespace) -> tuple[str, dict]:
    """Translate the selected subcommand into its protocol name and object args."""
    group, action = options.command, options.action
    if group == "query":
        return f"query.{action}", ({"afterSequence": options.after_sequence}
                                   if action == "console" else {})
    if group == "propose" and action == "edits":
        edits = []
        for subject, field, raw in options.edit:
            edits.append({"subject": subject, "field": field,
                          "value": json.loads(raw, parse_constant=_reject_constant)})
        return "propose.edits", {"summary": options.summary, "evidence": options.evidence,
                                 "edits": edits}
    if group == "propose":
        return "propose.withdraw", {"proposal": options.proposal}
    if group == "settings":
        return "settings.set", {options.name: options.value}
    if group == "debugview":
        if (options.topic == "final") != (options.value is None):
            raise ValueError("debugview set needs final alone or a topic and value")
        return "debugview.set", ({} if options.topic == "final" else
                                 {"topic": options.topic, "value": options.value})
    if group == "scene":
        return "scene.open", {"scene": options.scene}
    if group == "measure":
        return "measure.run", {"warmup": options.warmup, "frames": options.frames,
                               "name": options.name}
    if group == "capture" and action == "gpu":
        return "capture.gpu", {}
    if group == "capture" and action == "screenshot":
        return "capture.screenshot", {"name": options.name, "frames": options.frames}
    if group == "capture":
        return "capture.sequence", {"name": options.name, "frames": options.frames,
                                    "warmup": options.warmup}
    if group == "graph":
        return "graph.dump", {"name": options.name}
    if group == "plan":
        plan = json.loads(options.path.read_text(encoding="utf-8"),
                          parse_constant=_reject_constant)
        if not isinstance(plan, dict):
            raise ValueError("plan must be a JSON object")
        return "plan.submit", plan
    raise ValueError("Unknown bridge command")


def _bridge_parsers(commands: argparse._SubParsersAction) -> None:
    query = commands.add_parser("query", help="inspect the running editor")
    queries = query.add_subparsers(dest="action", required=True)
    for name in _QUERIES:
        item = queries.add_parser(name)
        if name == "console":
            item.add_argument("--after-sequence", type=int, default=0)

    propose = commands.add_parser("propose", help="submit or withdraw scene edits")
    proposals = propose.add_subparsers(dest="action", required=True)
    edits = proposals.add_parser("edits")
    edits.add_argument("--summary", required=True)
    edits.add_argument("--edit", nargs=3, metavar=("SUBJECT", "FIELD", "JSON"),
                       action="append", required=True)
    edits.add_argument("--evidence", action="append", default=[])
    proposals.add_parser("withdraw").add_argument("proposal", type=int)

    settings = commands.add_parser("settings")
    setting_actions = settings.add_subparsers(dest="action", required=True)
    set_command = setting_actions.add_parser("set")
    set_command.add_argument("name")
    set_command.add_argument("value")
    debugview = commands.add_parser("debugview")
    debug_actions = debugview.add_subparsers(dest="action", required=True)
    debug = debug_actions.add_parser("set")
    debug.add_argument("topic", choices=("final", "temporal", "lighting", "occlusion"))
    debug.add_argument("value", nargs="?")
    scene = commands.add_parser("scene")
    scene.add_subparsers(dest="action", required=True).add_parser("open").add_argument("scene")
    measure = commands.add_parser("measure")
    run = measure.add_subparsers(dest="action", required=True).add_parser("run")
    run.add_argument("warmup", type=int)
    run.add_argument("frames", type=int)
    run.add_argument("name")
    capture = commands.add_parser("capture")
    captures = capture.add_subparsers(dest="action", required=True)
    captures.add_parser("gpu")
    still = captures.add_parser("screenshot")
    still.add_argument("name")
    still.add_argument("frames", type=int)
    sequence = captures.add_parser("sequence")
    sequence.add_argument("name")
    sequence.add_argument("frames", type=int)
    sequence.add_argument("warmup", type=int)
    graph = commands.add_parser("graph")
    graph.add_subparsers(dest="action", required=True).add_parser("dump").add_argument("name")
    plan = commands.add_parser("plan")
    plan.add_subparsers(dest="action", required=True).add_parser("submit").add_argument("path", type=Path)


def main(argv: list[str] | None = None) -> int:
    """Run a sidecar operation or one bridge command with structured output."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true", help="run a local sidecar check")
    parser.add_argument("--socket", type=Path, help="editor socket (overrides environment and discovery)")
    parser.add_argument("--name", dest="client_name", default="lmx_session.py",
                        help="client name shown in the editor")
    parser.add_argument("--wait-tier", action="store_true",
                        help="poll query.status until the operator raises this connection's tier")
    parser.add_argument("--timeout", type=float, default=600.0, help="whole request timeout in seconds")
    commands = parser.add_subparsers(dest="command")
    sidecar = commands.add_parser("sidecar", help="write a scene proposal sidecar")
    sidecar.add_argument("document", type=Path)
    sidecar.add_argument("--actor", required=True)
    sidecar.add_argument("--summary", required=True)
    sidecar.add_argument("--evidence", action="append", type=Path, default=[])
    _bridge_parsers(commands)
    args = parser.parse_args(argv)
    if args.selftest:
        selftest()
        return 0
    if args.command == "sidecar":
        try:
            output = write_sidecar(args.document, args.actor, args.summary, args.evidence)
        except (ValueError, OSError) as error:
            print(f"sidecar: {error}", file=sys.stderr)
            return 1
        print(output)
        return 0
    if args.command is None:
        parser.error("choose a command or --selftest")
    try:
        command, command_args = _arguments(args)
        explicit = args.socket or (Path(os.environ["LMX_SESSION_SOCKET"])
                                   if os.environ.get("LMX_SESSION_SOCKET") else None)
        paths = [explicit] if explicit else discover_sockets()
        response = send_command(paths, args.timeout, args.client_name, command, command_args,
                                args.wait_tier)
    except (ValueError, OSError, json.JSONDecodeError) as error:
        response = {"id": 2, "ok": False,
                    "error": {"code": "invalid", "message": str(error)}}
        print(json.dumps(response, ensure_ascii=False))
        return 2
    except TransportError as error:
        response = {"id": 2, "ok": False,
                    "error": {"code": "transport", "message": str(error)}}
        print(json.dumps(response, ensure_ascii=False))
        return 3
    print(json.dumps(response, ensure_ascii=False))
    return 0 if response["ok"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
