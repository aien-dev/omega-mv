"""Public families: split train (8 families) and dev (4 families). 4 variants each.

Every oracle below is an independent reference implementation written by OmegaEval.
"""
from benchlib import mk, line, lines, ints, out, numlist, text_lines, words_line, M64

TRAIN = ["sumlist", "countb", "extrema", "gcdlcm", "reverse", "multiples", "caesar", "digits"]
DEV = ["primes", "fibo", "wordst", "brackets"]


def _sumlist():
    F = "sumlist"
    g = lambda r: numlist(r, 1, 200, (1 << 64) - 1)
    small = lambda r: numlist(r, 1, 200, 1 << 31)
    return [
        mk(F, "v1", "Read one line of numbers. Print their sum modulo 2^64.",
           lambda i: out(sum(ints(i)) % M64), ["1 2 3\n", "18446744073709551615 2\n"], g,
           ["0\n", "18446744073709551615 18446744073709551615 18446744073709551615\n"]),
        mk(F, "v2", "Read one line of numbers, each below 2^32. Print the sum of their squares modulo 2^64.",
           lambda i: out(sum(x * x for x in ints(i)) % M64), ["1 2 3\n", "4294967295\n"], small,
           ["0\n", "4294967295 4294967295 4294967295\n"]),
        mk(F, "v3", "Read one line of numbers. Print the sum of the even numbers modulo 2^64 (print 0 if there are none).",
           lambda i: out(sum(x for x in ints(i) if x % 2 == 0) % M64), ["1 2 3 4\n", "7\n"], g,
           ["1 3 5\n", "18446744073709551614 18446744073709551614\n"]),
        mk(F, "v4", "Read one line of numbers. Print the sum, modulo 2^64, of the numbers at even positions (the 1st, 3rd, 5th and so on).",
           lambda i: out(sum(ints(i)[0::2]) % M64), ["5 100 7\n", "9\n"], g, ["0 1\n"]),
    ]


def _countb():
    F = "countb"
    alpha = "abcxyz AB0123456789\n"
    g = lambda r: text_lines(r, r.randint(1, 30), alpha.replace("\n", ""), 60)
    return [
        mk(F, "v1", "Count the bytes equal to lowercase letter a (byte 97) in the whole input. Print the count.",
           lambda i: out(i.count(b"a")), ["banana\n", "xyz\n"], g, ["\n", "a\n" * 1000]),
        mk(F, "v2", "Count the newline bytes (byte 10) in the input. Print the count.",
           lambda i: out(i.count(b"\n")), ["one\ntwo\n", "x\n"], g, ["\n" * 5000]),
        mk(F, "v3", "Count the decimal digit bytes 0 to 9 (bytes 48 to 57) in the input. Print the count.",
           lambda i: out(sum(1 for c in i if 48 <= c <= 57)), ["a1b22\n", "none\n"], g, ["\n", "9" * 65000 + "\n"]),
        mk(F, "v4", "Count the space bytes (byte 32) in the input. Print the count.",
           lambda i: out(i.count(b" ")), ["a b c\n", "abc\n"], g, ["\n", " " * 3000 + "\n"]),
    ]


def _extrema():
    F = "extrema"
    g = lambda r: numlist(r, 1, 300, (1 << 64) - 1)
    def argmax(xs):
        m = max(xs)
        return xs.index(m)
    return [
        mk(F, "v1", "Read one line of at least one number. Print the largest.",
           lambda i: out(max(ints(i))), ["3 9 2\n", "5\n"], g, ["0\n", "18446744073709551615 0\n"]),
        mk(F, "v2", "Read one line of at least one number. Print the smallest.",
           lambda i: out(min(ints(i))), ["3 9 2\n", "5\n"], g, ["18446744073709551615\n", "7 0 7\n"]),
        mk(F, "v3", "Read one line of at least one number. Print the largest minus the smallest.",
           lambda i: out(max(ints(i)) - min(ints(i))), ["3 9 2\n", "5\n"], g, ["0 18446744073709551615\n"]),
        mk(F, "v4", "Read one line of at least one number. Print the position (counting from 0) of the first occurrence of the largest number.",
           lambda i: out(argmax(ints(i))), ["3 9 2 9\n", "5\n"], g, ["1 1 1\n", "0 0 18446744073709551615\n"]),
    ]


def _gcdlcm():
    from math import gcd
    F = "gcdlcm"
    g2 = lambda r: line(r.randint(1, (1 << 64) - 1), r.randint(1, (1 << 64) - 1))
    def g2c(r):
        k = r.randint(1, 1 << 20)
        return line(k * r.randint(1, 1 << 20), k * r.randint(1, 1 << 20))
    def glist(r):
        k = r.randint(1, 1000)
        return line(*[k * r.randint(1, 1 << 40) for _ in range(r.randint(1, 50))])
    def glcm(r):
        return line(r.randint(1, 1 << 31), r.randint(1, 1 << 31))
    def gl(i):
        xs = ints(i)
        r = 0
        for x in xs:
            r = gcd(r, x)
        return out(r)
    return [
        mk(F, "v1", "Read two positive numbers on one line. Print their greatest common divisor.",
           lambda i: out(gcd(*ints(i))), ["12 18\n", "7 13\n"], g2c, ["1 1\n", "18446744073709551615 18446744073709551615\n"] + [None] * 0),
        mk(F, "v2", "Read one line of at least one positive number. Print the greatest common divisor of all of them.",
           gl, ["12 18 30\n", "7\n"], glist, ["18446744073709551615\n"]),
        mk(F, "v3", "Read two positive numbers below 2^32 on one line. Print their least common multiple.",
           lambda i: out(ints(i)[0] * ints(i)[1] // gcd(*ints(i))), ["4 6\n", "7 13\n"], glcm,
           ["4294967295 4294967294\n", "1 1\n"]),
        mk(F, "v4", "Read two positive numbers on one line. Print yes if their greatest common divisor is 1, otherwise print no.",
           lambda i: out("yes" if gcd(*ints(i)) == 1 else "no"), ["4 9\n", "6 9\n"], g2,
           ["1 1\n", "18446744073709551615 18446744073709551614\n", "2 18446744073709551614\n"]),
    ]


def _reverse():
    F = "reverse"
    alpha = "abcdefghij XYZ"
    g = lambda r: text_lines(r, 1, alpha, 200, 1)
    gw = lambda r: words_line(r, 1, 40)
    gm = lambda r: text_lines(r, r.randint(1, 20), "abcdef", 30)
    gn = lambda r: line(r.randint(0, (1 << 64) - 1))
    def rev_lines(i):
        rows = i.split(b"\n")[:-1]
        return b"".join(x[::-1] + b"\n" for x in rows), 0
    return [
        mk(F, "v1", "Read one line of text. Print the line's bytes in reverse order, followed by a newline.",
           lambda i: (i[:-1][::-1] + b"\n", 0), ["abc\n", "a b\n"], g, ["x\n", "\n"]),
        mk(F, "v2", "Read one line of words separated by single spaces. Print the words in reverse order, separated by single spaces.",
           lambda i: (b" ".join(i[:-1].split(b" ")[::-1]) + b"\n", 0), ["one two three\n", "solo\n"], gw),
        mk(F, "v3", "Read lines of text. Print every line with its bytes reversed, keeping the order of the lines.",
           rev_lines, ["abc\nde\n", "\n"], gm),
        mk(F, "v4", "Read one number. Print its decimal digits in reverse order with leading zeros removed (print 0 for 0).",
           lambda i: out(int(str(ints(i)[0])[::-1])), ["1200\n", "345\n"], gn, ["0\n", "18446744073709551615\n", "10\n"]),
    ]


def _multiples():
    F = "multiples"
    gn = lambda r: line(r.randint(1, 2000))
    gb = lambda r: line(r.randint(1, 10 ** 9))
    def fizz(i):
        n = ints(i)[0]
        rows = []
        for k in range(1, n + 1):
            rows.append("FizzBuzz" if k % 15 == 0 else "Fizz" if k % 3 == 0 else "Buzz" if k % 5 == 0 else str(k))
        return ("\n".join(rows) + "\n").encode(), 0
    return [
        mk(F, "v1", "Read one number n between 1 and 2000. For k from 1 to n print one line: FizzBuzz if k is divisible by 15, otherwise Fizz if divisible by 3, otherwise Buzz if divisible by 5, otherwise k.",
           fizz, ["5\n", "15\n"], gn, ["1\n", "2000\n"]),
        mk(F, "v2", "Read one number n below 10^9. Print how many numbers k with 1 <= k < n are divisible by 3 or by 5.",
           lambda i: out(sum(1 for k in range(1, ints(i)[0]) if k % 3 == 0 or k % 5 == 0) if ints(i)[0] < 100000 else ((ints(i)[0] - 1) // 3 + (ints(i)[0] - 1) // 5 - (ints(i)[0] - 1) // 15)),
           ["10\n", "1\n"], gb, ["0\n", "999999999\n"]),
        mk(F, "v3", "Read one number n below 10^9. Print the sum of all numbers k with 1 <= k < n that are divisible by 3 or by 5.",
           lambda i: out(sum(k for k in range(1, ints(i)[0]) if k % 3 == 0 or k % 5 == 0)) if ints(i)[0] < 100000 else out(_s35(ints(i)[0])),
           ["10\n", "1\n"], gb, ["0\n", "999999999\n"]),
        mk(F, "v4", "Read one number n between 1 and 2000. Print n lines: line k (from 1) holds k*k.",
           lambda i: (b"".join(line(k * k) for k in range(1, ints(i)[0] + 1)), 0), ["3\n", "1\n"], gn, ["2000\n"]),
    ]


def _s35(n):
    def s(d):
        m = (n - 1) // d
        return d * m * (m + 1) // 2
    return s(3) + s(5) - s(15)


def _caesar():
    F = "caesar"
    alpha = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789.,"
    g = lambda r: text_lines(r, r.randint(1, 10), alpha, 80)
    def shift(k):
        def f(i):
            o = bytearray()
            for c in i:
                if 97 <= c <= 122:
                    o.append((c - 97 + k) % 26 + 97)
                elif 65 <= c <= 90:
                    o.append((c - 65 + k) % 26 + 65)
                else:
                    o.append(c)
            return bytes(o), 0
        return f
    tasks = []
    for vid, k in (("v1", 1), ("v2", 3), ("v3", 13), ("v4", 25)):
        tasks.append(mk(F, vid, f"Copy the input to the output, replacing every ASCII letter by the letter {k} places later in the alphabet, wrapping from z to a and from Z to A and keeping its case. All other bytes, including newlines, are copied unchanged.",
                        shift(k), ["Hello, World\n", "xyz XYZ\n"], g, ["\n", "zZ\n"]))
    return tasks


def _digits():
    F = "digits"
    g = lambda r: line(r.randint(0, (1 << 64) - 1))
    def droot(n):
        while n >= 10:
            n = sum(int(c) for c in str(n))
        return n
    def pnz(n):
        p = 1
        for c in str(n):
            if c != "0":
                p *= int(c)
        return p
    return [
        mk(F, "v1", "Read one number. Print the sum of its decimal digits.",
           lambda i: out(sum(int(c) for c in str(ints(i)[0]))), ["123\n", "0\n"], g, ["18446744073709551615\n", "9999999999999999999\n"]),
        mk(F, "v2", "Read one number. Repeatedly replace it by the sum of its decimal digits until it is below 10. Print the result.",
           lambda i: out(droot(ints(i)[0])), ["38\n", "0\n"], g, ["18446744073709551615\n", "9\n", "10\n"]),
        mk(F, "v3", "Read one number. Print how many decimal digits it has (0 has one digit).",
           lambda i: out(len(str(ints(i)[0]))), ["123\n", "0\n"], g, ["18446744073709551615\n", "10000000000000000000\n", "9\n"]),
        mk(F, "v4", "Read one number. Print the product of its nonzero decimal digits (print 1 if it has none).",
           lambda i: out(pnz(ints(i)[0])), ["1203\n", "0\n"], g, ["18446744073709551615\n", "9999999999999999999\n"]),
    ]


# ---------------- dev ----------------

def _primes():
    F = "primes"
    def isprime(n):
        if n < 2:
            return False
        if n % 2 == 0:
            return n == 2
        d = 3
        while d * d <= n:
            if n % d == 0:
                return False
            d += 2
        return True
    def sieve(n):
        s = bytearray([1]) * (n + 1)
        s[0:2] = b"\x00\x00"[: min(2, n + 1)]
        for p in range(2, int(n ** 0.5) + 1):
            if s[p]:
                s[p * p :: p] = bytearray(len(s[p * p :: p]))
        return s
    S = sieve(2_000_000)
    PR = [i for i in range(len(S)) if S[i]]
    def lpf(n):
        best, d = 1, 2
        while d * d <= n:
            while n % d == 0:
                best, n = d, n // d
            d += 1
        return max(best, n) if n > 1 else best
    return [
        mk(F, "v1", "Read one number below 10^12. Print prime if it is a prime number, otherwise print composite (0 and 1 are composite for this task).",
           lambda i: out("prime" if isprime(ints(i)[0]) else "composite"), ["7\n", "1\n"],
           lambda r: line(r.choice([r.randint(0, 10 ** 12 - 1), r.choice(PR[-2000:]) * 1, r.choice(PR[1000:5000]) * r.choice(PR[1000:5000])])),
           ["0\n", "2\n", "999999999989\n", "999966000289\n"]),
        mk(F, "v2", "Read one number n below 1000000. Print how many primes are less than or equal to n.",
           lambda i: out(sum(S[: ints(i)[0] + 1])), ["10\n", "1\n"], lambda r: line(r.randint(0, 999999)), ["0\n", "2\n", "999999\n"]),
        mk(F, "v3", "Read one number n between 1 and 10000. Print the n-th prime number (the 1st prime is 2).",
           lambda i: out(PR[ints(i)[0] - 1]), ["1\n", "5\n"], lambda r: line(r.randint(1, 10000)), ["10000\n"]),
        mk(F, "v4", "Read one number between 2 and 10^12. Print its largest prime factor.",
           lambda i: out(lpf(ints(i)[0])), ["12\n", "13\n"], lambda r: line(r.randint(2, 10 ** 12)),
           ["2\n", "1000000000000\n", "999999999989\n", "999966000289\n"]),
    ]


def _fibo():
    F = "fibo"
    def fib(n, m=None):
        a, b_ = 0, 1
        for _ in range(n):
            a, b_ = b_, a + b_
            if m:
                a, b_ = a % m, b_ % m
        return a
    def count_le(n):
        a, b_, c = 0, 1, 0
        while a <= n:
            c += 1
            a, b_ = b_, a + b_
        return c
    def even_sum(n):
        a, b_, s = 0, 1, 0
        while a <= n:
            if a % 2 == 0:
                s += a
            a, b_ = b_, a + b_
        return s
    return [
        mk(F, "v1", "Read one number n below 100000. Print the n-th Fibonacci number modulo 2^64, where F(0)=0 and F(1)=1.",
           lambda i: out(fib(ints(i)[0], M64)), ["10\n", "0\n"], lambda r: line(r.randint(0, 99999)), ["1\n", "93\n", "94\n", "99999\n"]),
        mk(F, "v2", "Read two numbers n and m on one line, n below 1000000 and m between 1 and 10^9. Print F(n) modulo m, where F(0)=0 and F(1)=1.",
           lambda i: out(fib(ints(i)[0], ints(i)[1]) % ints(i)[1]), ["10 7\n", "0 1\n"],
           lambda r: line(r.randint(0, 999999), r.randint(1, 10 ** 9)), ["1 1\n", "999999 1000000000\n"]),
        mk(F, "v3", "Read one number n. Print how many terms of the sequence F(0)=0, F(1)=1, F(k)=F(k-1)+F(k-2) are less than or equal to n (count F(1) and F(2) separately).",
           lambda i: out(count_le(ints(i)[0])), ["1\n", "10\n"], lambda r: line(r.randint(0, (1 << 64) - 1)), ["0\n", "18446744073709551615\n"]),
        mk(F, "v4", "Read one number n below 10^18. Print the sum of the even Fibonacci numbers that are less than or equal to n (F(0)=0, F(1)=1).",
           lambda i: out(even_sum(ints(i)[0])), ["10\n", "1\n"], lambda r: line(r.randint(0, 10 ** 18 - 1)), ["0\n", "999999999999999999\n"]),
    ]


def _wordst():
    F = "wordst"
    alpha = "abcdefgABCDEFG"
    def gw(r):
        rows = []
        for _ in range(r.randint(1, 20)):
            rows.append(" ".join("".join(r.choice(alpha) for _ in range(r.randint(1, 12))) for _ in range(r.randint(0, 10))))
        return ("\n".join(rows) + "\n").encode()
    def words(i):
        return i.split()
    return [
        mk(F, "v1", "The input is lines of words; words are separated by single spaces and a line may be empty. Print the number of words.",
           lambda i: out(len(words(i))), ["a bb\nccc\n", "\n"], gw),
        mk(F, "v2", "The input is lines of words; words are separated by single spaces and a line may be empty. Print the length in bytes of the longest word (0 if there are no words).",
           lambda i: out(max([len(w) for w in words(i)] or [0])), ["a bbb\ncc\n", "\n"], gw),
        mk(F, "v3", "The input is lines of words; words are separated by single spaces and a line may be empty. Print how many words start with an uppercase letter A to Z.",
           lambda i: out(sum(1 for w in words(i) if 65 <= w[0] <= 90)), ["Ab cd Ef\n", "\n"], gw),
        mk(F, "v4", "The input is lines of words; words are separated by single spaces and a line may be empty. Print the number of the line (counting from 1) that has the most words; on a tie print the smallest such line number.",
           lambda i: out(1 + max(range(len(i.split(b'\n')[:-1])), key=lambda k: (len(i.split(b'\n')[k].split()), -k))), ["a\nb c\n", "x\n"], gw, ["\n\n\n"]),
    ]


def _brackets():
    F = "brackets"
    def gp(r, al="()"):
        n = r.randint(0, 400)
        if r.random() < 0.5:
            s, st = [], []
            pairs = {"(": ")", "[": "]", "{": "}"}
            opens = [c for c in al if c in pairs]
            for _ in range(n):
                if st and r.random() < 0.5:
                    s.append(pairs[st.pop()])
                else:
                    c = r.choice(opens)
                    st.append(c)
                    s.append(c)
            while st:
                s.append(pairs[st.pop()])
            s = "".join(s)
            if r.random() < 0.3 and s:
                k = r.randrange(len(s))
                s = s[:k] + s[k + 1:]
        else:
            s = "".join(r.choice(al) for _ in range(n))
        return (s + "\n").encode()
    def bal(i, al):
        pairs = {")": "(", "]": "[", "}": "{"}
        st = []
        for k, c in enumerate(i[:-1].decode()):
            if c in "([{":
                st.append(c)
            else:
                if not st or st[-1] != pairs[c]:
                    return False, k
                st.pop()
        return not st, (len(i) - 1 if st else -1)
    def depth(i):
        d = m = 0
        for c in i[:-1].decode():
            d += 1 if c == "(" else -1
            m = max(m, d)
        return m
    def first_bad(i):
        st = []
        for k, c in enumerate(i[:-1].decode()):
            if c == "(":
                st.append(k)
            elif st:
                st.pop()
            else:
                return k
        return st[0] if st else -1
    return [
        mk(F, "v1", "Read one line made only of ( and ) characters (possibly empty). Print yes if the parentheses are balanced, otherwise no.",
           lambda i: out("yes" if bal(i, "()")[0] else "no"), ["(())()\n", "())\n"], lambda r: gp(r), ["\n", ")(\n", "((\n", "(()\n"]),
        mk(F, "v2", "Read one line made only of ( and ) characters that is balanced. Print the maximum nesting depth (0 for an empty line).",
           lambda i: out(depth(i)), ["(())()\n", "\n"], lambda r: _balanced(r), ["\n", "(" * 30000 + ")" * 30000 + "\n"]),
        mk(F, "v3", "Read one line made only of the characters ( ) [ ] { } (possibly empty). Print yes if every bracket is closed by the matching kind in the right order, otherwise no.",
           lambda i: out("yes" if bal(i, "()[]{}")[0] else "no"), ["([]{})\n", "([)]\n"], lambda r: gp(r, "()[]{}"), ["\n", "}\n", "([\n", "{}(\n"]),
        mk(F, "v4", "Read one line made only of ( and ) characters. Scanning left to right, a ) with no open ( before it is unmatched; a ( never closed is unmatched. Print the position (counting from 0) of the first unmatched ) if there is one, otherwise of the leftmost unmatched (, otherwise print none.",
           lambda i: out("none" if first_bad(i) < 0 else first_bad(i)), ["(()\n", "()\n"], lambda r: gp(r), ["\n", ")\n", "(((\n"]),
    ]


def _balanced(r):
    n = r.randint(0, 300)
    s, d = [], 0
    for _ in range(n):
        if d and r.random() < 0.5:
            s.append(")")
            d -= 1
        else:
            s.append("(")
            d += 1
    s.append(")" * d)
    return ("".join(s) + "\n").encode()


BUILDERS = {
    "sumlist": _sumlist, "countb": _countb, "extrema": _extrema, "gcdlcm": _gcdlcm, "reverse": _reverse,
    "multiples": _multiples, "caesar": _caesar, "digits": _digits,
    "primes": _primes, "fibo": _fibo, "wordst": _wordst, "brackets": _brackets,
}
SPLIT = {**{f: "train" for f in TRAIN}, **{f: "dev" for f in DEV}}
