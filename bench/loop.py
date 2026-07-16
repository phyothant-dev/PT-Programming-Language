import time

start = time.time()
s = sum(range(1, 10_000_001))
elapsed = time.time() - start
print(f"Sum: {s}")
print(f"Time: {elapsed:.4f}s")
