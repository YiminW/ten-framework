# Sequential request TTFB verification

As of: 2026-10-27 (Asia/Shanghai, UTC+08:00)
Codebase commit: `a452b6d9ed66f9c57fa1d4a80cf5f3473f61b170`

## Scope and assertions

Run only `test_metrics.py::test_sequential_requests_ttfb`, with seven configurations:
ElevenLabs TTS/TTD, ByteDance standard/duplex, MiniMax standard/duplex, and Soul.
The first request streams text and then ends; after completion, a second request
contains one final text. Each must emit audio, finish, and report one TTFB.
Reported TTFB must be nonnegative and no larger than tester send-to-first-audio
elapsed time. Supported provider observers also require agreement within 2 ms.
An unsupported observer is recorded as unsupported; it is not independent
provider-boundary verification. ElevenLabs currently uses the tester bound.

These are live provider tests. They require valid credentials and may consume
provider quota. They are different from the deterministic delayed-connection
mock tests. A live pass alone cannot prove exclusion of connection delays.
HTTP and other provider timing improvements are deferred to a later batch.

## Preparation

Use a prepared Linux TEN application with `ten_packages/system/{ten_runtime,
ten_runtime_python,ten_ai_base}` and the six Python extension packages at the
revisions intended for verification. Prepare the Python interpreter with the
extension requirements, pytest, pytest-asyncio, python-dotenv and the guarder
requirements before running. The runner does not download or publish packages.
If an extension imports another local Python package, add that package's parent
directory to `PYTHONPATH`.

Create seven configuration folders under a private configuration root, named:

- `elevenlabs-tts` (model `eleven_flash_v2_5`)
- `elevenlabs-ttd` (a dialogue model, for example `eleven_v4`)
- `bytedance_tts`
- `bytedance_tts_duplex`
- `minimax_tts_websocket`
- `minimax_tts_websocket_duplex`
- `soul_tts_python`

Each folder contains `property_basic_audio_setting1.json` using that extension's
configuration schema. Use the deployed extension's own valid property/config
as the starting point. Set model and voice explicitly; do not copy standard
MiniMax configuration into duplex without checking its schema. Credentials
can be `${env:VARIABLE}` placeholders. Keep configuration and output outside
the repository, and never commit credentials or raw provider logs.

## Run and capture evidence

Run inside the container, substituting the prepared paths:

```bash
uv run --no-project --python /path/to/prepared/python python \
  /path/to/tts_guarder/scripts/run_ttfb.py \
  --app-dir /path/to/prepared-app \
  --extension-dir /path/to/extensions \
  --config-root /path/to/private-configs \
  --python /path/to/prepared/python \
  --env-file /path/to/private.env \
  --output /tmp/ttfb-review-$(date +%Y%m%d-%H%M%S)
```

Repeat `--env-file` to load multiple dotenv files; later files override earlier
ones. Use `--collect-only` first to validate the seven test invocations without
calling providers. Collection is labeled COLLECTED, never PASS.

The runner creates an isolated temporary app using the specified packages and
current guarder tests. It runs each case in a separate process, continues after
failures and returns a nonzero exit status if any case fails. An existing output
folder is rejected to preserve prior results. Capture the terminal's TTFB SUMMARY
and source/configuration paths for review screenshots. Keep the accompanying
`summary.txt`, `results.json`, per-case pytest logs and JUnit XML for diagnosis.
Logs may include credentials from provider configuration; inspect and redact
before sharing. Do not screenshot raw debug logs.
