"""om-bench (#653) task framework: family records, oracles and seeded input generators.

Independent oracle code written by OmegaEval. Candidate lanes (OmegaIntent, OmegaMind, OmegaCorpus)
never edit this file or the sealed families. Python here is a verification oracle outside the
delivered Omega build closure (disclosed in CONTRACT.md section 9).
"""
import hashlib, hmac, os, random

# Private sealed material (held-out families, keys, opaque-id key) lives outside the repository.
SEALED = os.environ.get("OMB_SEALED", os.path.expanduser("~/handoffs/OmegaEval/sealed"))
OPAQUE_KEY = os.path.join(SEALED, "opaque.key")


def opaque(split, tid):
    """Public name of a task. Train/dev ids are public; sealed ids are an HMAC-SHA256 under a private key,
    so a reader cannot recover a sealed family name by hashing guesses."""
    if split in ("train", "dev") or tid.startswith(("zz-", "ctl-")):
        return tid
    key = open(OPAQUE_KEY, "rb").read().strip()
    return split + ":" + hmac.new(key, tid.encode(), hashlib.sha256).hexdigest()[:10]

M64 = 1 << 64

COMMON_IO = (
    "Input arrives on standard input as ASCII text of at most 65536 bytes. Unless the task says "
    "otherwise, numbers are unsigned decimal integers below 2^64 with no sign and no leading zeros, "
    "separated by single spaces, and every input line ends with one newline byte (10). Write the "
    "answer to standard output exactly as specified, each output line ending with one newline byte. "
    "Exit with status 0 unless the task says otherwise. Write nothing to standard output that the task "
    "does not ask for."
)


class Task:
    def __init__(self, family, vid, statement, oracle, examples, hidden):
        self.family = family
        self.vid = vid
        self.statement = statement
        self.oracle = oracle
        self.examples = examples
        self.hidden = hidden

    @property
    def tid(self):
        return f"{self.family}.{self.vid}"


def b(s):
    return s.encode("ascii") if isinstance(s, str) else s


def line(*vals):
    return (" ".join(str(v) for v in vals) + "\n").encode("ascii")


def lines(rows):
    return b"".join(line(*r) if isinstance(r, (list, tuple)) else line(r) for r in rows)


def ints(inp):
    return [int(t) for t in inp.split()]


def out(*vals):
    return line(*vals), 0


def rng_for(tid, salt="v1"):
    return random.Random(f"om-bench-653/{salt}/{tid}")


def numlist(rng, n_lo, n_hi, v_hi):
    n = rng.randint(n_lo, n_hi)
    return line(*[rng.randint(0, v_hi) for _ in range(n)])


def text_lines(rng, n_lines, alphabet, max_len, min_len=0):
    rows = []
    for _ in range(n_lines):
        k = rng.randint(min_len, max_len)
        rows.append("".join(rng.choice(alphabet) for _ in range(k)))
    return ("\n".join(rows) + "\n").encode("ascii")


def words_line(rng, n_lo, n_hi, alphabet="abcdefghijklmnopqrstuvwxyz", wmax=8):
    n = rng.randint(n_lo, n_hi)
    ws = ["".join(rng.choice(alphabet) for _ in range(rng.randint(1, wmax))) for _ in range(n)]
    return (" ".join(ws) + "\n").encode("ascii")


def mk(family, vid, statement, oracle, examples, gen, boundary=(), n_random=6):
    """examples: visible inputs; gen(rng) -> one hidden input; boundary: fixed hidden inputs."""
    tid = f"{family}.{vid}"
    rng = rng_for(tid)
    hidden = [b(x) for x in boundary] + [gen(rng) for _ in range(n_random)]
    return Task(family, vid, statement, oracle, [b(x) for x in examples], hidden)
