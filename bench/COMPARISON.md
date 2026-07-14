# PT Performance Comparison

Benchmark results comparing PT to other interpreted languages. All tests run on macOS (Apple Silicon) with default interpreter settings.

> **Note:** PT is a tree-walk interpreter written in C++17. It's designed for simplicity and learning, not raw speed. Compiled languages like C, Go, or Rust would be orders of magnitude faster than all interpreters below.

## Benchmark Results

### Recursive Fibonacci — `fib(30)`

Tests function call overhead and recursion depth.

| Language | Time | Relative |
|----------|------|----------|
| **Node.js** (V8) | 0.016s | 1× (fastest) |
| **Python 3** | 0.052s | 3.3× |
| **Ruby** | 0.052s | 3.3× |
| **PT** | 28.48s | 1780× |

### Loop Sum — Sum 1 to 10,000,000

Tests loop overhead and arithmetic.

| Language | Time | Relative |
|----------|------|----------|
| **Node.js** (V8) | 0.010s | 1× (fastest) |
| **Python 3** | 0.078s | 7.8× |
| **Ruby** | 0.003s | 0.3× |
| **PT** | 20.31s | 2031× |

### String Concatenation — 100,000 iterations

Tests string allocation and memory handling.

| Language | Time | Relative |
|----------|------|----------|
| **Node.js** (V8) | 0.004s | 1× (fastest) |
| **Python 3** | 0.139s | 34.8× |
| **Ruby** | 0.243s | 60.8× |
| **PT** | 0.634s | 158.5× |

### Array Push + Iterate — 100,000 items

Tests array allocation, push, and indexed access.

| Language | Time | Relative |
|----------|------|----------|
| **Python 3** | 0.002s | 1× (fastest) |
| **Ruby** | 0.003s | 1.5× |
| **Node.js** (V8) | 0.004s | 2× |
| **PT** | 0.559s | 279.5× |

## Summary

| Benchmark | PT vs Python | PT vs Node.js | PT vs Ruby |
|-----------|-------------|---------------|------------|
| Fibonacci | ~550× slower | ~1780× slower | ~550× slower |
| Loop | ~260× slower | ~2030× slower | ~6770× slower |
| Strings | ~4.5× slower | ~159× slower | ~2.6× slower |
| Arrays | ~280× slower | ~140× slower | ~193× slower |

## Why is PT slower?

PT is a **tree-walk interpreter** — it walks the AST directly without compilation or JIT. The main overhead sources:

1. **Per-expression allocation** — every `evaluate()` call creates a new `PTValue` on the heap
2. **Scope lookups** — variables resolved by walking up the environment chain each time
3. **No JIT** — Node.js (V8) and modern Python have JIT/optimizing compilers that hot-loop optimize
4. **String copying** — string concatenation creates new strings (immutable value semantics)
5. **Function call overhead** — each call creates a new `Environment` with shared_ptr allocations

## Where PT is competitive

- **String operations** — PT's builtins (`replace`, `split`, `join`, `substr`) call C++ `std::string` directly, so they're only ~3-5× slower than Python
- **Array operations** — `push`, `pop`, `len` use `std::vector` underneath, keeping overhead reasonable
- **I/O bound tasks** — HTTP server, file I/O, and database operations are dominated by system call time, not interpreter overhead

## Design Philosophy

PT prioritizes:

1. **Readability** — `show`, `let`, `fn`, `unless`, `match` are English-like
2. **Simplicity** — single-pass lexing, no AST optimization passes
3. **Learning** — easy to read the C++ source (~3000 lines)
4. **Safety** — no raw pointers, bounds checking on arrays

Raw speed was never a goal. If you need performance-critical code, write that module in C++ and call it from PT's built-in functions.
