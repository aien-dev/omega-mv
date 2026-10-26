# seed/net: HTTP/1.1 in seeded Omega

`http.om` is a bounded HTTP/1.1 server and client written in seeded Omega (OM-2 profile),
compiled by `seed/out/omc`. It covers strict message parsing, Content-Length, chunked and
close-delimited bodies, keep-alive, pipelining, `Expect: 100-continue`, deadlines and
server-sent events (SSE) in both directions. TLS is not included; see "Transport boundary".

Status: IMPLEMENTED and EXERCISED on a Linux AArch64 host with `seed/out/omc` sha256
`befb8199...8a1d` (`tests/check.py` PASS; the run log is attached to the integration PR). Host
only: no hardware or deployment claim.

## Files

| Path | What it is |
|---|---|
| `http.om` | The library unit. No `om2` line, no `fn main`, no globals. |
| `examples/sse_server.om` | An embeddable server: `/health`, `/events` (SSE), `/echo` (body up to 1 MiB). |
| `examples/sse_client.om` | An incremental SSE client that prints one line per event. |
| `tests/parse.om` | Memory-mode parser driver for the corpus. |
| `tests/live.om` | Loopback socket tests: bodies, keep-alive, pipelining, SSE, malformed requests, deadlines, descriptor leaks. |
| `tests/check.py` | Host referee: builds everything, runs the corpus, the mutants, `live.om` and the curl / http.client end-to-end check. |

## Building a program

The library has no `om2` line, so the application build adds one line in front of the
concatenated units:

```sh
{ printf 'om2\n'; cat seed/net/http.om app.om; } | seed/out/omc > app
```

A program that also parses JSON adds `seed/json/json.om` in the same `cat`. Names in
`http.om` are functions `h*`, constants `H*` and texts `h*`; an application must not reuse
them. `big`, `out`, `part`, `at`, `field` and the other OM-2 words cannot be names.

## Ownership and state

All state belongs to the caller. A connection is two arrays, normally locals of the function
that owns the connection:

```
cs = new ulong HCSLEN 0      state: descriptor, cursors, deadlines, framing, header index
io = new uchar 131072 0      bytes: read region | write region | transport region
```

`hinit cs io fd role` splits `io` into a read region (`io[0, HRCAP)`), a write region
(`io[HWB, HWB + HWCAP)`, a quarter of `io`, 256 to 65536 bytes, changeable with `hsetw`) and an
empty transport region. Role 1 is a server, 0 a client. `hinit` takes ownership of `fd`:
`hclose cs` closes it exactly once, and `hinit` with `fd < 0` selects memory mode (no I/O,
`hmemld` loads a whole message; used by the corpus).

Offsets stored in `cs` (method, target, header names and values) are absolute offsets into `io`
and stay valid until the next `hreqrd` / `hrsprd` on the same `cs`. Copy what must outlive that.
Several connections are several `cs`/`io` pairs; nothing is shared between them. Because omc
refuses globals as arguments and two parts of one array in one call (error 19), the library uses
no global, so there is no single-state limitation: a server may hold as many connections as it
has arrays for, and a forked worker gets its own copy.

Bounded epochs: every loop in the library has a bound derived from a buffer size; a connection
carries at most `HMAXMSG` messages (default 1000, set with `put cs HMAXMSG n`) after which the
server answers with `Connection: close`; a server process serves a bounded number of connections
per run (the examples take it as an argument) and is restarted by its supervisor.

## Limits (all settable with `put cs SLOT v` after `hinit`)

| Slot | Default | Meaning |
|---|---|---|
| `HHEADMX` | min(16384, HRCAP / 2) | Bytes of one message head, also one chunk-size or trailer line. Over it: `HEHEAD`. |
| `HHMAX` | 64 (`HHSLOTS`, the maximum) | Header fields per message, also trailer fields. Over it: `HEHEAD`. |
| `HBCAP` | 1048576 | Decoded body bytes per message. Over it: `HEBIG`. Covers the 300 KiB gateway bodies and the 1 MiB catalog cap. |
| `HMAXMSG` | 1000 | Messages per connection. |
| `HIDLE` (`hsettmo`) | 30 s | Longest single wait for the socket. |
| `HHDLV` (`hsettmo`) | 30 s | Deadline for receiving a whole head, from the start of `hreqrd` / `hrsprd`. |
| `HDL` (`hsetdl`) | none | Absolute deadline for the following body reads and writes. |

## Results

Every function returns a `long`. Nonnegative is success. Negative values:

| Value | Name | Meaning (server answers with `hreject`) |
|---|---|---|
| `-errno` | | Linux error from the socket, e.g. -104, -32 |
| -110 | `HETIME` | idle wait or deadline passed (408) |
| -400 | `HEBAD` | malformed message (400) |
| -413 | `HEBIG` | body over `HBCAP`, or larger than the caller's array in `hbodyall` (413) |
| -417 | `HEEXP` | `Expect` other than `100-continue` (417) |
| -431 | `HEHEAD` | head, line or field count over its cap (431) |
| -499 | `HETRUNC` | the peer closed in the middle of a message (no answer) |
| -501 | `HETE` | a transfer coding other than `chunked` (501) |
| -505 | `HEVER` | HTTP major version other than 1, or 1.x with x > 1 (505) |
| -22, -95 | `HEINVAL`, `HENOSUP` | caller error; unsupported transport |

## Parsing rules (RFC 9112)

- Lines end in CRLF. A bare LF or a CR not followed by LF is `HEBAD`. A server skips empty
  lines before a request line.
- Request line: method = 1..32 tchar, one SP, target = 1..8192 bytes 0x21-0x7e in origin form,
  `*`, absolute form (`scheme://...`) or, for CONNECT only, authority form; one SP;
  `HTTP/1.1` or `HTTP/1.0`. Status line: version, SP, three digits 100..599, SP, reason of
  field-value bytes (the SP is required even when the reason is empty).
- Field line: a tchar name, `:` with no whitespace before it, a value of HTAB, SP, VCHAR and
  obs-text (no NUL, CR, LF or DEL); leading and trailing SP/HTAB are not part of the value. A
  line that starts with SP or HTAB (obs-fold) is `HEBAD`.
- `Content-Length`: 1..19 digits on exactly one field line. A repeated field, even with the
  same value, or a list is `HEBAD`.
- `Transfer-Encoding`: exactly one field line whose value is `chunked` (any case). Any other
  coding or list is `HETE`; with `Content-Length` or in HTTP/1.0 it is `HEBAD`.
- HTTP/1.1 requests need exactly one `Host`. `Connection: close` ends the connection after the
  message; HTTP/1.0 connections always close (no 1.0 keep-alive).
- Response bodies follow RFC 9112 6.3: none for HEAD, 1xx, 204 and 304; chunked; Content-Length;
  otherwise close-delimited. `hrsprd` skips up to 15 interim 1xx responses and returns 101 as
  final.
- Chunk sizes are 1..15 hex digits, optionally followed directly by `;` and extension bytes
  (field-value bytes, not otherwise interpreted). Whitespace before `;` is `HEBAD`. Trailer
  fields are validated like header fields and then discarded.

## Server API

```
lf = hlisten a backlog             a: sockaddr bytes (haddr4 a ip port; port 0 = ephemeral).
                                   The bound address is written back: hport a is the port.
c  = haccept lf tmo                wait at most tmo ns (negative = forever); O_NONBLOCK, CLOEXEC
r  = hinit cs io int c 1           take ownership of c
hsettmo cs idle headdl
r  = hreqrd cs io                  1 head read; 0 peer closed between requests; < 0 error
     hreject cs io r               answer an error result and mark the connection closing
hismeth cs io m / hispath cs io p  method / path (target before '?') comparisons
i  = hhfind cs io name             header index or -1; value io[hhvoff cs i, + hhvlen cs i)
n  = hbodyrd cs io d               next decoded body bytes into d; 0 at the end
n  = hbodyall cs io d              the whole body (HEBIG when d is too small)
hrspst cs io code                  status line with the standard reason
hhname cs io n / hhval cs io v / hhnum cs io v     one header field; framing fields refused
hhend cs io mode clen              0 Content-Length clen, 1 chunked, 2 close-delimited
hput cs io bytes / hputb cs io b   body bytes (chunked: one chunk per buffer flush)
hflush cs io                       send what is buffered now
hwfin cs io                        end the body; checks a Content-Length body was clen bytes
hrspempty / hrsperr cs io code     a complete empty response (hrsperr also closes)
hnext cs io                        1 = read the next request on this connection, 0 = close
hclose cs
```

Responses to HEAD keep their framing fields and drop the body bytes; 1xx, 204 and 304 get no
framing field and no body. `put cs HKEEP 0` before `hhend` ends the connection after this
response. A request body the handler did not read makes `hnext` return 0.

## Client API

```
fd = hdial a tmo                   nonblocking connect bounded by tmo ns
r  = hinit cs io int fd 0
hreqst cs io method / hreqtg cs io target / hhname / hhval / hhend / hput / hwfin
st = hrsprd cs io                  final status (put cs HISHEAD 1 first after a raw HEAD)
n  = hbodyrd cs io d               Content-Length, chunked or close-delimited
```

## Server-sent events

Writing: `hrspsse` sends the 200 head (`text/event-stream`, `Cache-Control: no-cache`, chunked),
then per event `hsseevt` (type), `hsseid`, `hsseret` (retry ms), `hssecmt` (comment), `hssedat`
(data; CR, LF and CRLF in it become separate `data:` lines), and `hsseend`, which ends the event
and flushes it as one chunk. `hwfin` ends the stream.

Reading (WHATWG HTML 9.2.6): `ss = new ulong HSSLEN 0`, `lb = new uchar N 0` (N >= 256),
`hsseinit ss`. Feed any slice of the body with `hssefeed ss lb src`; it returns the bytes it
consumed and stops after a dispatching line with `get ss HSRDY` = 1. The event is then: type
`lb[0, HSELEN)` (empty means `message`), last event id `lb[64, 64 + HSILEN)`, data
`lb[128, 128 + HSDLEN)`, retry `get ss HSRETRY`. Call `hssenext ss` and feed the rest. Lines may
end in CR, LF or CRLF and may be split anywhere between calls; a leading BOM is removed. Event
type and id are at most 64 bytes, data plus the current line at most N - 128 bytes (`HEHEAD` /
`HEBIG` otherwise). A gateway relays an upstream stream incrementally with `hbodyrd` into a
small array, `hssefeed`, and `hsseevt`/`hsseid`/`hssedat`/`hsseend` downstream.

## Transport boundary

Every byte crosses three functions only: `hrecvio` (into the read region), `hrecvd` (into the
caller's body array) and `hsendio` (from the write region), plus `hwait`. Sockets are
O_NONBLOCK; a call that would block waits with `waitio` for at most min(idle, deadline - now),
so there is no busy wait and no unbounded block. `get cs HXPORT` selects the transport: 0 is
plain TCP or Unix sockets; any other value returns `HENOSUP` today. The TLS lane (OmegaTLS,
`tls_read`/`tls_write` with their own monotonic deadline and whole-slice writes) plugs in by
keeping its connection state in the transport region `io[HTB, HTB + HTL)` and calling it from
those three functions; no other function changes.

## Host words used

`socknew bind listen accept connect sockname sockopt send recv waitio fclose monons`
(OMEGESE section 10). No new compiler word is needed. Tests also use `fork waitpid sleepns
fopen getdnts`.

## Checks

```sh
nice -n 19 ionice -c3 python3 seed/net/tests/check.py [--omc seed/out/omc] [--keep DIR]
```

Prints `HTTP-CHECK PASS` and exits 0, `FAIL` (exit 1) or `NOT_RUN` (exit 3, e.g. no curl).
Stages:

1. Corpus: about 100 hand cases with written expectations plus 1500 deterministic fuzz cases
   (seed 788; generated messages, mutations of framing, whitespace, bytes and truncation). The
   expected line for each comes from an independent Python reference parser written from RFC
   9112. The reference is itself checked against the hand table and, for responses, against
   the standard library's `http.client`: a response `http.client` rejects must be rejected,
   and one both accept must agree on status, version, fields and body. The corpus caps are
   head 2048 bytes, 16 fields, body 4096 bytes. The run fails on any missing output line.
2. Mutants: ten single-word changes to `http.om` (for example `cl te` to `cl cl`, hex base 16
   to 10, `ge` to `gt` on the field cap); each must compile and must change the corpus output.
3. `tests/live.om`: forks a loopback server and checks 300 KiB and 1 MiB request bodies
   (Content-Length and chunked), 300 KiB responses in both framings, HEAD, two pipelined
   requests, 40 SSE events read 7 bytes at a time, eight malformed requests and their status,
   `Expect: 100-continue` over the cap, a 200 ms head deadline against a stalled client, a
   200 ms idle wait against a silent server, refused connections, and that the process ends
   with the descriptors it started with.
4. End to end: `examples/sse_server.om` on an ephemeral port, driven by `curl` (SSE bytes,
   `-i` head bytes, 300 KiB echo with `Expect: 100-continue` and chunked), `http.client`
   (six requests on one keep-alive connection, the last with `Connection: close`), raw sockets (obs-fold 400, empty connection,
   HTTP/1.0) and `examples/sse_client.om`; the server's descriptor count must return to its
   start value after every connection, and the server must exit 0.

Every socket in the checks is 127.0.0.1 with a port chosen by the kernel. Nothing contacts port
18091 or any outside host.
