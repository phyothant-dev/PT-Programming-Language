import time

def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

start = time.time()
result = fib(35)
elapsed = time.time() - start
print(f"fib(35) = {result}")
print(f"Time: {elapsed:.4f}s")
