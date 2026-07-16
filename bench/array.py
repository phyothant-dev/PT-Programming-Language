import time

start = time.time()
arr = [i * 2 for i in range(100_000)]
s = sum(arr)
elapsed = time.time() - start
print(f"Sum: {s}")
print(f"Time: {elapsed:.4f}s")
