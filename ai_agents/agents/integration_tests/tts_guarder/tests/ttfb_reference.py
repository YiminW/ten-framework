"""Independent first-text/first-audio observations at public provider boundaries."""

import os
from contextlib import contextmanager
from importlib import import_module
from threading import RLock

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

    def current_request(self):
        with self.lock:
            return self.current_request_id

    def text_sent(self, request_id, timestamp):
        with self.lock:
            if request_id == self.current_request_id:
                self.sent.setdefault(request_id, timestamp)

    def audio_received(self, request_id, timestamp):
        with self.lock:
            if request_id == self.current_request_id and request_id in self.sent:
                self.received.setdefault(request_id, timestamp)

    def elapsed_ms(self, request_id):
        with self.lock:
            if request_id not in self.sent or request_id not in self.received:
                return None
            return (self.received[request_id] - self.sent[request_id]) / 1_000_000


HOOKS = {
    "elevenlabs_multiple_context_tts_python": (
        "elevenlabs",
        "ElevenLabsObserver",
        "elevenlabs_tts",
        "wss://api.elevenlabs.io",
    ),
    "bytedance_tts": ("bytedance", "ByteDanceObserver", "bytedance_tts", None),
    "bytedance_tts_duplex": (
        "bytedance_duplex",
        "ByteDanceDuplexObserver",
        "bytedance_tts",
        None,
    ),
    "minimax_tts_websocket": (
        "minimax",
        "MiniMaxObserver",
        "minimax_tts",
        "wss://api.minimaxi.com/ws/v1/t2a_v2",
    ),
    "minimax_tts_websocket_duplex": (
        "minimax",
        "MiniMaxObserver",
        "minimax_tts",
        "wss://api.minimax.cn/ws/v1/t2a_v2_bidi",
    ),
    "soul_tts_python": (
        "soul",
        "SoulObserver",
        "soul_tts",
        "wss://qa-soul-llm.soulapp.cn/llm",
    ),
}


def resolve_url(config, default):
    params = config.get("params") or {}
    url = (
        config.get("url")
        or config.get("api_url")
        or config.get("base_url")
        or params.get("url")
        or params.get("api_url")
        or params.get("base_url")
    )
    if url and url.startswith("${env:") and url.endswith("}"):
        variable, _, fallback = url[6:-1].partition("|")
        url = os.environ.get(variable, fallback)
    return url or default


@contextmanager
def install_ttfb_reference(extension_name, config, monkeypatch):
    """Install a fresh protocol observer per connection; restore it on exit."""
    spec = HOOKS.get(extension_name)
    if spec is None:
        yield None
        return
    hook_module, hook_class, filename, default = spec
    if extension_name == "elevenlabs_multiple_context_tts_python":
        config_module = import_module(f"ten_packages.extension.{extension_name}.config")
        model = (config.get("params") or {}).get("model_id") or config.get(
            "model_id", ""
        )
        if config_module.uses_ttd_protocol(model):
            filename = "elevenlabs_ttd"
    url = resolve_url(config, default)
    if not url:
        raise ValueError(f"No provider URL for supported TTFB hook: {extension_name}")
    module = import_module(f"ten_packages.extension.{extension_name}.{filename}")
    hook = getattr(import_module(f".ttfb_hooks.{hook_module}", __package__), hook_class)
    reference = TTFBReference()
    with monkeypatch.context() as scoped:
        scoped.setattr(
            module,
            "websockets",
            ObservedWebSockets(
                module.websockets,
                url,
                None,
                match_path=extension_name != "elevenlabs_multiple_context_tts_python",
                observer_factory=lambda: hook(reference),
            ),
        )
        yield reference
