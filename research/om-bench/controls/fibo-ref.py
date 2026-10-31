import sys
n = int(sys.stdin.read().split()[0])
a, b = 0, 1
for _ in range(n):
    a, b = b, (a + b) % (1 << 64)
print(a)
