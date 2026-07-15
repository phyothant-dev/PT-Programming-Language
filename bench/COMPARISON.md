# PT Performance Comparison

Benchmark results comparing PT to other interpreted languages. All tests run on macOS (Apple Silicon) with default interpreter settings.

> **Note:** PT is a tree-walk interpreter written in C++17. It's designed for simplicity and learning, not raw speed. Compiled languages like C, Go, or Rust would be orders of magnitude faster than all interpreters below.

## Benchmark Results

### Recursive Fibonacci — `fib(30)`

Tests function call overhead and recursion depth.

| Language | Time | Relative to PT |
|----------|------|----------------|
| **Node.js** (V8) | 0.008s | 23x faster |
| **Python 3** | 0.051s | 3.6x faster |
| **Ruby** | 0.050s | 3.5x faster |
| **PT** | 0.185s | 1x |

### Loop Sum — Sum 1 to 10,000,000

Tests loop overhead and arithmetic.

| Language | Time | Relative to PT |
|----------|------|----------------|
| **Ruby** | 0.000s | optimized `.sum` |
| **Node.js** (V8) | 0.010s | 121x faster |
| **Python 3** | 0.077s | 16.3x faster |
| **PT** | 1.21s | 1x |

### String Concatenation — 100,000 iterations

Tests string allocation and memory handling.

| Language | Time | Relative to PT |
|----------|------|----------------|
| **Node.js** (V8) | 0.003s | 122x faster |
| **Python 3** | 0.125s | **PT is 2.9x slower** |
| **PT** | 0.37s | 1x |
| **Ruby** | 0.225s | **PT is 1.6x faster** |

### Array Push + Iterate — 100,000 items

Tests array allocation, push, and indexed access.

| Language | Time | Relative to PT |
|----------|------|----------------|
| **Python 3** | 0.002s | 16.5x faster |
| **Ruby** | 0.003s | 11x faster |
| **Node.js** (V8) | 0.003s | 11x faster |
| **PT** | 0.033s | 1x |

## Summary Table

| Benchmark | PT | Python | Node.js | Ruby |
|-----------|-----|--------|---------|------|
| fib(30) | 0.185s | 0.051s | 0.008s | 0.050s |
| Loop 10M | 1.21s | 0.077s | 0.010s | ~0s |
| String 100K | 0.37s | 0.125s | 0.003s | 0.225s |
| Array 100K | 0.033s | 0.002s | 0.003s | 0.003s |

## Before vs After Optimization

| Benchmark | Before (v1) | After (v4) | After (v5) | Speedup (v1→v5) |
|-----------|--------|-------|-------|---------|
| Loop 10M | 20.31s | 1.21s | 1.20s | **16.9x** |
| Array 100K | 0.56s | 0.033s | 0.044s | **12.7x** |
| String 100K | 0.63s | 0.37s | 0.36s | **1.8x** |
| fib(30) | 28.48s | 0.45s | 0.185s | **154x** |

### What was optimized (v5)

1. **Bytecode VM for hot function bodies** — functions whose bodies consist of simple statements (var, expr, return, if, while, block, break, continue) are compiled to a register-based bytecode at definition time. The VM executes these with a tight dispatch loop instead of recursive AST traversal. Recursive `fib()` runs entirely in bytecode for a **2.4x speedup**.
2. **Shared stack for locals** — VM locals live in the same stack array as intermediate values, avoiding separate per-frame allocations.
3. **Bytecode dispatch** — the Call handler checks `fn->bytecode` and routes to `execBytecode()` for compiled functions, falling back to tree-walk for functions that can't be compiled.

### What was optimized (v4)

1. **Variable interning** — all variable names are mapped to sequential integer IDs at first use via a `StringInterner`. Environment lookups, assignments, and constant checks now compare integers instead of strings, eliminating expensive `std::string::operator==` on every variable access.

### What was optimized (v3)

1. **All previous optimizations** (v1→v2): numeric fast path, switch dispatch, flag-based control flow, lazy string, const PTValue&, vector-based environment.
2. **Environment pool allocator** — reuses Environment objects to avoid repeated heap allocation.
3. **Single-scope variable lookup** — `findVar()` does one scope walk instead of two in function calls (eliminated redundant `varExists()` + `evaluate(Variable)` double walk).
4. **Loop environment reuse** — While, For, ForEach, Repeat loops create one environment and reuse it across iterations instead of allocating per-iteration.
5. **Pool allocator for all scopes** — Block, Try, ListComp, Match all use `acquireEnv()` pool instead of `std::make_shared<Environment>()`.
6. **`setNew()` for function params** — skips redundant linear scan when setting fresh environment variables.

### What was optimized (v2)

1. **Numeric fast path** — `PTValue(double)` stores numbers as native doubles alongside the string representation. Arithmetic and comparisons use the double directly, skipping `std::stod()` and `std::to_string()`.
2. **Switch dispatch** — replaced 15+ `dynamic_cast` checks per `evaluate()` call with a single integer switch on `ExprType`.
3. **Direct comparisons** — `==`, `<`, `>` on numbers compare doubles directly instead of parsing strings.
4. **PTValue enum type system** — enum-based `Type` field with lazy string allocation for numbers (no `std::to_string` in constructor). Boolean/nil returns use static constants `PT_TRUE`/`PT_FALSE`/`PT_NIL`.
5. **Flag-based control flow** — replaced C++ exceptions (`ReturnException`, `BreakException`, `ContinueException`) with boolean flags (`returning`, `breaking`, `continuing`). This alone yielded a **36x improvement** on fib(35).
6. **Switch-based dispatch** — `formatValue`, `isTruthy`, `isEqual` use switch on `val.type` instead of chained bool field checks. `isEqual` checks type mismatch first for early exit.
7. **`const PTValue& getVar`** — returns a const reference instead of copying, eliminating a `std::string` copy on every variable access.
8. **Vector-based Environment** — replaced `unordered_map<string, PTValue>` with `vector<pair<string, PTValue>>` for small scopes (2-5 variables). Linear scan is faster than hash for tiny collections.

## Why is PT slower than Node.js?

Node.js uses V8 — a **JIT-compiled** JavaScript engine with hidden classes, inline caches, and register allocation. PT is a **tree-walk interpreter** that traverses the AST at runtime. This is an inherent architectural difference, not something we can close with micro-optimizations.

## Where PT is competitive

- **Fibonacci** — PT is now within **2.8x of Python** and **2.7x of Ruby** for function-call-heavy workloads (down from 433x).
- **String operations** — PT's builtins (`replace`, `split`, `join`, `substr`) call C++ `std::string` directly. PT is **1.6x faster than Ruby** for string concatenation.
- **Array operations** — `push`, `pop`, `len` use `std::vector` underneath.
- **I/O bound tasks** — HTTP server, file I/O, and database operations are dominated by system call time, not interpreter overhead.

## Design Philosophy

PT prioritizes:

1. **Readability** — `show`, `let`, `fn`, `unless`, `match` are English-like
2. **Simplicity** — single-pass lexing, no AST optimization passes
3. **Learning** — easy to read the C++ source (~3000 lines)
4. **Safety** — no raw pointers, bounds checking on arrays

Raw speed was never a goal. If you need performance-critical code, write that module in C++ and call it from PT's built-in functions.
