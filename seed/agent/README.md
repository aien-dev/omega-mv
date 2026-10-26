# seed/agent: omagent, a small coding agent in seeded Omega

omagent is an agent harness in the style of HKUDS/nanobot (MIT), written in seeded Omega.
Only nanobot's architecture was reused. None of its code was copied. The program is one
static ARM64 Linux executable (188,084 bytes at this commit) built from four sources:

```sh
{ printf 'om2\n'; cat seed/json/json.om seed/json/jsonw.om seed/net/http.om seed/agent/agent.om; } \
  | seed/out/omc > omagent && chmod +x omagent
```

The only compiler needed is the pinned `seed/out/omc`. Every implementation byte is compiled
from `.om`. Python appears only in the test referee `tests/check.py`, which also plays the mock
model server.

Status: IMPLEMENTED and EXERCISED on the host (Linux ARM64, DGX Spark) against a scripted mock
server. No real model server was used, and no hardware qualification applies.

## Architecture

```
operator CLI / stdin line  ->  agent loop  ->  model backend (HTTP POST, SSE or JSON)
                                   |                 |
                                   v                 v
                           tool dispatcher    stream parser (http.om SSE + json.om)
                     (read_file, write_file,         |
                      run_command, http_fetch)       v
                                   |          stdout (content deltas as they arrive)
                                   v
                     session file (append-only JSONL), memory file (read-only)
```

### Agent loop

One user turn runs at most `--max-rounds` model rounds (default 8, at most 64):

1. Append the user message to the history.
2. Send the system prompt, the memory file and the whole history to the model with the
   four tool schemas.
3. Stream the reply. Content deltas go to stdout as they arrive. Tool call deltas are
   collected by `index`, at most 8 calls per reply.
4. With no tool calls, the turn ends. Otherwise, run every call in order, append one `tool`
   message per call (matched by `tool_call_id`), then go to step 2.
5. Hitting the round limit stops with exit 7. Interrupted turns are trimmed from the
   in-memory history, so a later turn never sends a dangling `tool_calls` message.

With `-p PROMPT`, omagent runs one turn and exits. Without it, it reads prompts from stdin,
one per line, until end of file (the stdin channel).

### Model backend

Any OpenAI-compatible chat completions endpoint reached over plain HTTP at an IPv4 literal
or `localhost`. The request is `{"model":..,"stream":true,"tools":[..],"messages":[..]}`
with `Content-Length`, `Accept: text/event-stream` and, when a key is present,
`Authorization: Bearer KEY`. A `text/event-stream` reply is parsed as SSE until `[DONE]`.
Any other content type is read as one JSON document (`choices[0].message`), which covers
servers that ignore `stream`.

`https://` is refused with exit 2: this build carries no TLS client. Wiring `om/tls` in is
proposed and NOT_RUN. Point omagent only at a model server you run. The resident server on
127.0.0.1:18091 must not be used for tests.

### Tool protocol

The tools are OpenAI function tools whose `arguments` is a JSON object (a JSON string holding
an object, or a raw object, both accepted):

| tool | arguments | permission | result |
|---|---|---|---|
| `read_file` | `path` | always | file bytes, at most 64 KiB plus a truncation marker |
| `write_file` | `path`, `content` | `--allow-write` | `ok: wrote N bytes to PATH` |
| `run_command` | `argv` (1..63 strings) | `argv[0]` exactly equal to an `--allow-cmd` entry | output, then `[exit status N]`, `[killed by signal N]` or `[killed: timeout after S s]` |
| `http_fetch` | `url` (`http://` only) | canonical `HOST:PORT` on an `--allow-fetch` entry | `status=N` then the body, at most 64 KiB |

A refused call is answered with a tool message that starts with `refused:` and names the
permission, and the loop continues. The model sees the refusal; nothing runs. Result text is
repaired to valid UTF-8 before it enters the history.

### Memory

- Session memory: `--session FILE` is an append-only JSONL file (opened `O_APPEND|O_CREAT`,
  mode 0600). Each line is one message object. At start-up, the last 768 KiB is loaded. Any
  line that is not a valid message object stops omagent with exit 8 before contacting the
  model. This includes a final line cut short by a crash; the operator then deletes that line.
- History limits: each message is at most 256 KiB. Older messages are dropped from the request,
  at a user-message boundary, when the history passes 512 KiB or 4096 messages. The session file
  keeps everything.
- Long-term memory: `--memory FILE` (at most 64 KiB, read once) is appended to the system prompt.
  omagent never writes it. Keep it outside the workspace if the model must not edit it
  through `write_file`.

Relative `--session` and `--memory` paths resolve against the directory omagent started in,
before it enters `--workdir`. Keep the session file outside the workspace too.

### Channels

Two channels exist now: the CLI one-shot (`-p`) and stdin lines. Both are first-party. These are
PROPOSED and not built: an HTTP channel (an OpenAI-compatible `/v1/chat/completions` server using
the `seed/net` server half, like `nanobot serve`) and chat adapters (Matrix, IRC, Telegram).
Adapters need TLS and are the natural home for omega-bridge or other universal adapters.

## Safety model

1. **The model is untrusted.** Every byte it sends passes `http.om` (status line, headers,
   chunked or close-delimited body, SSE framing) and `json.om` under fixed caps: 256 KiB per
   JSON reply or SSE event, 128 KiB reply text, 8 tool calls, and per call 255 bytes of id,
   127 bytes of name and 64 KiB of arguments. A reply that breaks a cap, is not JSON, carries
   an `error` object, or ends before `[DONE]` or a `finish_reason` stops omagent with exit 6.
   Nothing from that reply runs.
2. **Tool permissions are explicit.** By default, only `read_file` inside the workspace is
   allowed. Writing, programs and fetches each need their own flag. Programs are named by
   absolute path, and there is no shell and no PATH search.
3. **The workspace is a jail by path rules.**
   - Paths must be relative.
   - No part may be empty, `.` or `..`. No control byte is allowed.
   - Every prefix is checked with `lstat` and must not be a symbolic link.
   - The final open uses `O_NOFOLLOW`.
   - Limit: the checks and the open are separate system calls, so a concurrent local process
     that swaps a directory for a link between them could escape (TOCTOU). Proposed fix:
     `openat2` with `RESOLVE_BENEATH` as a host word. The effect-manifest work in `seed/effects`
     is the proposed outer sandbox layer.
4. **Commands are bounded.**
   - Children run with an empty environment and stdin on `/dev/null`.
   - stdout and stderr go to one close-on-exec pipe, and at most 64 KiB is kept.
   - At the `--cmd-timeout` deadline (default 10 s), the child gets SIGKILL and is reaped.
   - Grandchildren are not killed: there are no process groups yet. One that keeps the pipe
     open only delays omagent until the deadline.
5. **No secrets in logs.**
   - The API key is read from `OMAGENT_API_KEY` (or `--key-env NAME`), never from argv. It must
     be bytes 0x21..0x7e.
   - The key appears only in the Authorization header. The header buffer is zeroed after the
     write.
   - stderr carries only fixed messages and `omagent: tool NAME: ok|refused|error`.
   - Tool arguments and results are never echoed to stderr.
   - The session file stores messages, not headers.
   - The tests assert that the key never appears in stdout, stderr or the session file, and
     that the mock server did receive it.
6. **Timeouts.** `--timeout` (default 120 s) bounds each model request, both idle time and the
   whole body. Connecting has a 5 s limit.

## CLI

```
omagent --url http://IP:PORT/PATH --model NAME [-p PROMPT] [--workdir DIR]
  [--session FILE] [--memory FILE] [--allow-cmd /ABS/PATH]... [--allow-write]
  [--allow-fetch HOST:PORT]... [--timeout S] [--cmd-timeout S] [--max-rounds N]
  [--key-env NAME] | --version | --help
```

Exit codes:

| code | meaning |
|---|---|
| 0 | ok |
| 2 | usage, configuration, `--workdir` or a bad key |
| 3 | connect or send failed |
| 4 | HTTP status other than 200 (stderr shows the status and the first 512 sanitized body bytes) |
| 5 | model timeout or read failure |
| 6 | malformed model output |
| 7 | `--max-rounds` reached |
| 8 | session or memory file |
| 9 | a message could not be encoded, or the history is full |

## Tests

```sh
nice -n 19 ionice -c3 python3 seed/agent/tests/check.py
```

The test builds omagent with `seed/out/omc` and starts a scripted mock model server on a
kernel-chosen 127.0.0.1 port. It then runs 32 conversations:

- plain SSE and JSON replies
- every tool, plus two calls in one reply
- refusals: a program not on the allowlist, programs disabled, `..`, an absolute path, a
  symbolic link, writing disabled, an unknown tool, a fetch host not on the allowlist, https
- malformed output: a bad event, a truncated stream, an error object, a bad call index,
  HTTP 500
- a model timeout, a command timeout and the round limit
- session reload, a corrupt session line, no key, two stdin lines, connection refused

Negative controls follow. Two wrong expectations must fail. Six source mutants must each be
caught: allowlist bypass, `..` allowed, symlink followed, fetch allowlist bypass, write flag
ignored, end of stream without `[DONE]` accepted. Output: `AGENT-CHECK PASS cases=32 mutants=6`.
Exit 0 means PASS, 1 FAIL, and 3 NOT_RUN (no compiler).

## Host dependencies

| dependency | version | purpose | Omega replacement |
|---|---|---|---|
| `seed/out/omc` | pin sha256 189ada82... (omega-mv main) | compiler | itself (seeded) |
| Linux ARM64 kernel | host | system calls through compiled host words | none (platform) |
| Python 3 | 3.12 on the Spark | test referee and mock server only, not on the product path | a `.om` mock server on `seed/net` (proposed) |

## Measured against nanobot

MEASURED on 2026-10-26: N=30 paired, interleaved runs per arm and core class on a DGX Spark under
load (loadavg about 13). The comparison is nanobot-ai 0.3.5 on Python 3.12.3. The client was
pinned with `taskset` to X925 cpu 5 or A725 cpu 1, and each turn was one streamed reply from a
test-only mock. Medians:

| | omagent | nanobot | ratio (Wolfram) |
|---|---|---|---|
| `--version` wall, X925 / A725 | 3.87 / 4.55 ms | 369.7 / 704.5 ms | 95.5x / 154.9x |
| one turn wall, X925 / A725 | 4.86 / 6.17 ms | 1005.2 / 1953.8 ms | 207.0x / 316.8x |
| max RSS, `--version` / turn | 1624 / 2584 KiB | 69684 / 142792 KiB | 42.9x / 55.3x |
| installed size | 188,084 bytes | 336,116,353 bytes of site-packages plus the interpreter | 1787x |

Wall times include a launcher that costs 3.15 ms (X925) for `/bin/true`. The harness is in
`bench/` (`bench.sh`, the test-only `mock_pong.py`, and `stats.wl` with the raw data). The raw rows
and the Wolfram output are filed in the operator handoff `OmegaBot/bench/`. nanobot does more per
turn (skills, templates, more tools), so this is not an equal-feature comparison.

## Limits and NOT_RUN

- TLS model endpoints and fetches: NOT_RUN (refused by design in this build).
- Real model servers (vLLM, llama.cpp, MAX): NOT_RUN. Only the scripted mock was used.
- One request at a time; no parallel tool calls; no process-group kill; the TOCTOU limit above.
