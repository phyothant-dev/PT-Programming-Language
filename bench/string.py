import time

start = time.time()
s = ""
for i in range(100_000):
    s += "x"
elapsed = time.time() - start
print(f"Length: {len(s)}")
print(f"Time: {elapsed:.4f}s")
