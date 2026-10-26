"""Independent first-text/first-audio observations at public provider boundaries."""

import base64
import gzip
import os
import struct
from contextlib import contextmanager
from importlib import import_module
import json
from threading import RLock
import time

from .timing_hooks.transport import ObservedWebSockets


class TTFBReference:
    def __init__(self):
        self.lock = RLock()
        self.current_request_id = None
        self.sent = {}
        self.received = {}

    def begin_request(self, request_id):
        with self.lock:
            self.current_request_id = request_id

    def text_sent(self):
        timestamp = time.monotonic_ns()
        with self.lock:
            if self.current_request_id is not None:
                self.sent.setdefault(self.current_request_id, timestamp)

    def audio_received(self):
        timestamp = time.monotonic_ns()
        with self.lock:
            request_id = self.current_request_id
            if request_id in self.sent:
                self.received.setdefault(request_id, timestamp)

    def elapsed_ms(self, request_id):
        with self.lock:
            if request_id not in self.sent or request_id not in self.received:
                return None
            return (
                self.received[request_id] - self.sent[request_id]
            ) / 1_000_000


WS_MODULES = {
    "bytedance_tts": "bytedance_tts",
    "bytedance_tts_duplex": "bytedance_tts",
    "minimax_tts_websocket": "minimax_tts",
    "minimax_tts_websocket_duplex": "minimax_tts",
    "soul_tts_python": "soul_tts",
}


def _soul_message(raw):
    """Decode the gateway envelope independently of the extension decoder."""
    if isinstance(raw, str):
        return json.loads(raw)
    magic, version, _command, serialization, compression, length, count = struct.unpack(
        "!IBBBBIB", raw[:13]
    )
    if magic != 0x736F756C or version != 1 or serialization != 1:
        raise ValueError("Unsupported Soul gateway envelope")
    if length != len(raw) - 13:
        raise ValueError("Invalid Soul gateway length")
    offset = 13
    for _ in range(count):
        _, size = struct.unpack("!BI", raw[offset : offset + 5])
        offset += 5 + size
        if offset > len(raw):
            raise ValueError("Invalid Soul gateway header")
    body = raw[offset:]
    if compression == 1:
        body = gzip.decompress(body)
    elif compression != 0:
        raise ValueError("Unsupported Soul gateway compression")
    return json.loads(body)


class WebSocketReference:
    def __init__(self, vendor, reference):
        self.vendor = vendor
        self.reference = reference

    def observe_send(self, raw):
        if self.vendor == "soul_tts_python":
            message = _soul_message(raw)
            if message.get("event") == "task_continue" and message.get("text"):
                self.reference.text_sent()
            return
        if self.vendor == "bytedance_tts":
            import gzip

            if isinstance(raw, bytes) and raw[1] >> 4 == 1:
                message = json.loads(gzip.decompress(raw[8:]))
                if message.get("request", {}).get("text"):
                    self.reference.text_sent()
            return
        if self.vendor == "bytedance_tts_duplex":
            if isinstance(raw, bytes) and len(raw) > 8:
                event = int.from_bytes(raw[4:8], "big")
                if event == 200:
                    self.reference.text_sent()
            return
        if isinstance(raw, str):
            message = json.loads(raw)
            if message.get("text") or message.get("data", {}).get("text"):
                self.reference.text_sent()

    def observe_receive(self, raw):
        if self.vendor == "soul_tts_python":
            message = _soul_message(raw)
            audio = (message.get("data") or {}).get("audio")
            if message.get("event") == "task_continued" and audio:
                if base64.b64decode(audio, validate=True):
                    self.reference.audio_received()
            return
        if self.vendor == "bytedance_tts":
            if isinstance(raw, bytes) and len(raw) > 12 and raw[1] >> 4 == 11:
                if int.from_bytes(raw[8:12], "big") > 0:
                    self.reference.audio_received()
            return
        if self.vendor == "bytedance_tts_duplex":
            if (
                isinstance(raw, bytes)
                and len(raw) > 8
                and int.from_bytes(raw[4:8], "big") == 352
            ):
                self.reference.audio_received()
            return
        if isinstance(raw, str):
            message = json.loads(raw)
            data = message.get("data") or {}
            if (
                message.get("audio")
                or data.get("audio")
                or data.get("audio_data")
            ):
                self.reference.audio_received()


@contextmanager
def install_ttfb_reference(extension_name, config, monkeypatch):
    """Unsupported transports remain explicitly without a boundary reference."""
    filename = WS_MODULES.get(extension_name)
    if filename is None:
        yield None
        return
    module = import_module(f"ten_packages.extension.{extension_name}.{filename}")
    reference = TTFBReference()
    url = config.get("url") or config.get("api_url") or config.get("base_url")
    if not url:
        params = config.get("params") or {}
        url = params.get("url") or params.get("api_url") or params.get("base_url")
    if extension_name == "soul_tts_python":
        if url and url.startswith("${env:") and url.endswith("}"):
            variable, _, default = url[6:-1].partition("|")
            url = os.environ.get(variable, default)
        if not url:
            from ten_packages.extension.soul_tts_python.config import (
                DEFAULT_SOUL_TTS_URL,
            )

            url = DEFAULT_SOUL_TTS_URL
    if not url:
        yield None
        return
    with monkeypatch.context() as scoped:
        observer = WebSocketReference(extension_name, reference)
        scoped.setattr(
            module,
            "websockets",
            ObservedWebSockets(module.websockets, url, observer),
        )
        yield reference
