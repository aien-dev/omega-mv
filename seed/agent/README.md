# seed/agent: omagent, a small coding agent in seeded Omega

omagent is an agent harness in the style of HKUDS/nanobot (MIT), written in seeded Omega.
Only nanobot's architecture was reused. None of its code was copied. The program is one
static ARM64 Linux executable (188,268 bytes with omc 2c824805) built from four sources:

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
| `seed/out/omc` | pin sha256 2c824805... (omega-mv main) | compiler | itself (seeded) |
| Linux ARM64 kernel | host | system calls through compiled host words | none (platform) |
| Python 3 | 3.12 on the Spark | test referee and mock server only, not on the product path | a `.om` mock server on `seed/net` (proposed) |
| nanobot-ai | 0.3.5 on Python 3.12.3, in a separate venv | comparison arm of `bench/` only | none needed (not a dependency of omagent) |

## Compared with nanobot

### Feature matrix

nanobot-ai 0.3.5 (paths relative to its installed `nanobot/` package) against omagent (`agent.om`
lines at this commit). Status: HAS (equivalent), PARTIAL (narrower), MISSING (absent).

| feature | nanobot evidence | omagent | omagent evidence |
|---|---|---|---|
| model/tool loop with an iteration cap | `agent/runner.py`, `maxToolIterations` `config/schema.py:129` | HAS | `agturn` :2237, `--max-rounds` :82, :161 |
| streamed replies (SSE) | `providers/openai_compat_provider.py:2124` | HAS | `agsse` :1498; non-stream JSON too, `agjsonbody` :1527 |
| read and write a file | `read_file`/`write_file` `agent/tools/filesystem.py:281,546` | HAS (no offset/limit/pages on read) | `agtread` :1762, `agtwrite` :1804 |
| edit_file, list_dir, find_files, grep, apply_patch | `filesystem.py:869,1111`, `search.py:215,514`, `apply_patch.py:84` | MISSING | |
| shell command | `exec` `agent/tools/shell.py:248` (through a shell) | PARTIAL: argv exec of allowlisted programs, no shell | `agtrun` :1965, `agallowed` :1849 |
| persistent exec sessions | `exec_session`, `list_exec_sessions` `exec_session.py:565,722` | MISSING | |
| web fetch | `web_fetch` `agent/tools/web.py:1114` (https, redirects, extraction) | PARTIAL: plain `http://` to allowlisted HOST:PORT, raw body | `agtfetch` :2039 |
| web search | `web_search` `web.py:367` | MISSING | |
| workspace confinement | `restrictToWorkspace` `schema.py:401` (default off) | HAS (always on: no `..`, absolute paths or symlinks) | `agpathok` :1735 |
| command deny-list / timeouts | built-in deny-list `shell.py` ~213-232, timeout `:97` | PARTIAL: allowlist instead of deny-list; timeout kills the child only | `agexec` :1926, `--cmd-timeout` :81 |
| MCP client (stdio, SSE, streamable HTTP) | `agent/tools/mcp.py:1365,1412`, `schema.py:362` | MISSING | |
| session history | `session/manager.py`, sessions dir | HAS (one JSONL file per `--session`) | `agsload` :1032, `agappend` :917 |
| history compaction / consolidation | Consolidator and AutoCompact `agent/loop.py:433,443`, summaries `agent/memory.py:803-824` | PARTIAL: drops the oldest whole turns, no summaries | `agcompact` :896 |
| long-term memory and Dream | `memory/MEMORY.md`, `DreamConfig` `schema.py:53` (gateway only) | PARTIAL: read-only `--memory` file, nothing written back | `agmemload` :755 |
| system prompt from workspace templates and skills | `cli/agent.py:154`, `skills/` (11 built-in) | MISSING (fixed system prompt plus memory file) | `agsysmsg` :1093 |
| skills | `skills/`, `disabledSkills` `schema.py:146` | MISSING | |
| providers, presets, fallback models | about 46 providers `schema.py:240`, `fallbackModels` `:128` | PARTIAL: one OpenAI-compatible endpoint, plain HTTP only, Bearer key | `agurl` :420, `agkeyload` :720 |
| subagents | `spawn` `agent/tools/spawn.py:63`, `agent/subagent.py:100` | MISSING | |
| messages and session tools, goals | `message.py:117`, `session_messages.py:81,153`, `sessions.py:88,165`, `long_task.py:159,290` | MISSING | |
| chat channels | 17 channels in `channels/` (Telegram, Slack, Discord, ...) | MISSING (CLI only: `-p` or stdin lines) | `agline` :2289 |
| WebUI, gateway, OpenAI-compatible API server | `webui/`, `GatewayConfig` `schema.py:353`, `ApiConfig` `:333` | MISSING | |
| cron and heartbeat | `cron` tool `agent/tools/cron.py:118`, heartbeat `schema.py:326` | MISSING | |
| image generation, CLI apps, `my` | `image_generation.py:121`, `cli_apps.py:99`, `self.py:149` | MISSING | |
| transcription | `TranscriptionConfig` `schema.py:42` | MISSING | |
| API key kept out of logs and child environments | | HAS (checked by every test case) | `tests/check.py` |

omagent lacks MCP, subagents, skills, web search, the editing and search tools, exec sessions,
summarizing memory, multiple providers, TLS, chat channels, the WebUI and API server, cron and
heartbeat, image generation and transcription. What it has is the four-tool coding loop above.

### Equal-work benchmark

`bench/equal_bench.py` runs both programs through the same session:
- the same prompt and the same scripted mock, `bench/equal_mock.py`;
- streamed replies, four model requests per session: `read_file`, then `write_file`, then one
  echo through the shell tool, then the final answer;
- the same advertised tools: nanobot is reduced to `read_file`, `write_file`, `exec` and
  `web_fetch` by `bench/nb_minimal.py`, because its config cannot drop the rest. It also has no
  MCP, no channels, every skill disabled, `restrictToWorkspace`, and a fresh session each run;
- the mock checks every tool result, and the driver checks the written file and the final answer.

Remaining differences:
- nanobot's system prompt is larger: 44,698 request bytes per session against 7,480.
- nanobot's command runs as `env echo gamma-789` through `sh`, because its workspace guard
  refuses `/bin/echo`. omagent runs `/bin/echo` directly.

A negative control (omagent without `--allow-write`) is graded BAD.

MEASURED on 2026-10-26 with omc 2c824805 (binary sha256 3eb8e93e..., 188,268 bytes):
- N=30 paired runs per core class and model delay, arm order alternating;
- client pinned with `taskset` to X925 cpu 5 or A725 cpu 1;
- all 240 runs graded ok;
- warm page cache, Spark under its usual background load.

Wall times include the same Python launcher and `/usr/bin/time` for both arms. Medians, with ratios
from Wolfram:

| measure | omagent | nanobot | nanobot/omagent |
|---|---|---|---|
| launch to first model request, X925 / A725 | 5.7 / 6.5 ms | 897 / 1845 ms | 169x / 290x |
| full session, instant mock, X925 / A725 | 13.4 / 19.5 ms | 1294 / 2439 ms | 101x / 136x |
| full session, mock at 50 ms per chunk, X925 / A725 | 1834 / 1846 ms | 3340 / 4456 ms | 1.82x / 2.34x |
| peak RSS | 2,584 KiB | 142,940 KiB | 55.3x |

On-disk footprint:
- omagent is the 188,268-byte static binary and nothing else.
- During the session, nanobot loaded 1,894 module files totaling 51,602,687 bytes: 3.06 MB of its
  own package, 41.65 MB of third-party packages and 6.90 MB of the standard library. On top of that
  comes the 7,845,048-byte interpreter, which makes 316x.
- nanobot's full installation is 336,116,353 bytes of site-packages, plus the standard library and
  the interpreter.

Once the model is slow, the harness matters much less. With the mock spending about 1.8 s per
session, omagent's advantage falls from 101x to 1.82x (X925) and from 136x to 2.34x (A725). The
absolute gap stays at 1.3 to 2.5 s per session, mostly nanobot's 0.9 to 1.8 s startup, and
amounts to 45% (X925) and 57% (A725) of nanobot's session time. Against a real model that takes
seconds per reply, the share is smaller again. NOT_RUN: a real model server.

Raw rows, machine state, the Wolfram input and output, and the footprint lists are filed in the
operator handoff `OmegaBot/eqbench/`.

## Limits and NOT_RUN

- TLS model endpoints and fetches: NOT_RUN (refused by design in this build).
- Real model servers (vLLM, llama.cpp, MAX): NOT_RUN. Only the scripted mock was used.
- One request at a time; no parallel tool calls; no process-group kill; the TOCTOU limit above.
