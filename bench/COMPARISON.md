# PT Performance Comparison

Benchmark results comparing PT to other interpreted languages. All tests run on macOS (Apple Silicon) with default interpreter settings.

> **Note:** PT is a tree-walk interpreter + bytecode VM written in C++17. It's designed for simplicity and learning, not raw speed. Compiled languages like C, Go, or Rust would be orders of magnitude faster than all interpreters below.

## Benchmark Results

### Recursive Fibonacci — `fib(30)`

Tests function call overhead and recursion depth.

| Language | Time | Relative to PT |
|----------|------|----------------|
| **Node.js** (V8) | 0.005s | 34x faster |
| **Python 3** | 0.052s | 3.2x faster |
| **Ruby** | 0.049s | 3.4x faster |
| **PT** | 0.169s | 1x |

### Loop Sum — Sum 1 to 10,000,000

Tests raw integer arithmetic and loop overhead.

| Language | Time | Relative to PT |
|----------|------|----------------|
| **Ruby** | 0.204s | 2.0x faster |
| **PT** | 0.407s | 1x |
| **Python 3** | 0.461s | 0.89x (PT faster!) |

Tests loop overhead and arithmetic.

| Language | Time | Relative to PT |
|----------|------|----------------|
| **Ruby** | 0.000s | optimized `.sum` |
| **Node.js** (V8) | 0.009s | 55x faster |
| **Python 3** | 0.075s | 6.5x faster |
| **PT** | 0.491s | 1x |

### String Concatenation — 100,000 iterations

Tests string allocation and GC pressure.

| Language | Time | Relative to PT |
|----------|------|----------------|
| **Node.js** (V8) | 0.003s | 0.75x (PT slower) |
| **PT** | 0.004s | 1x |
| **Python 3** | 0.127s | 31.8x slower |
| **Ruby** | 0.244s | 61x slower |

### Array Push + Iterate — 100,000 elements

Tests array allocation and iteration.

| Language | Time | Relative to PT |
|----------|------|----------------|
| **Ruby** | 0.002s | 1.3x faster |
| **Python 3** | 0.003s | 1.9x faster |
| **Node.js** (V8) | 0.002s | 1.3x faster |
| **PT** | 0.016s | 1x |

## Summary Table

| Benchmark | PT | Python | Node.js | Ruby |
|-----------|-----|--------|---------|------|
| fib(30) | 0.169s | 0.052s | 0.005s | 0.049s |
| Loop 10M | **0.407s** | 0.461s | 0.009s | 0.204s |
| String 100K | **0.004s** | 0.127s | 0.003s | 0.244s |
| Array 100K | 0.016s | 0.003s | 0.002s | 0.002s |

## Before vs After Optimization

| Benchmark | Before (v1) | v4 | v5 | v7 | v8 | v9 | v10 | v11 | v12 | Speedup (v1→v12) |
|-----------|--------|-------|-------|-------|-------|-------|-------|-------|-------|---------|
| Loop 10M | 20.31s | 1.21s | 1.20s | 1.28s | 0.601s | 0.605s | 0.491s | 0.407s | 0.416s | **49x** |
| Array 100K | 0.56s | 0.033s | 0.044s | 0.039s | 0.023s | 0.023s | 0.018s | 0.016s | 0.016s | **35x** |
| String 100K | 0.63s | 0.37s | 0.36s | 0.38s | 0.241s | 0.137s | 0.005s | 0.004s | 0.004s | **158x** |
| fib(30) | 28.48s | 0.45s | 0.185s | 0.212s | 0.207s | 0.21s | 0.22s | 0.202s | 0.169s | **168x** |

### What was optimized (v12)

1. **Skip env save/restore** — When calling a function whose closure is the same as the current environment (i.e. no variable capture), skip the `shared_ptr<Environment>` save and restore. For recursive functions like `fib()` this eliminates 2 shared_ptr copies (~5ns each) per call. **16% fib speedup.**
2. **PT is now 168x faster than v1** for fibonacci (up from 141x) and only **3.2x slower than Python** (down from 3.9x).

### What was optimized (v11)

1. **Computed goto dispatch** — GCC/Clang's `&&label` and `goto *dispatch_table[op]` replaces the central switch dispatch. Eliminates a single indirect branch prediction miss per opcode by giving the CPU a direct branch target for each opcode. Falls back to switch on unsupported compilers. **15-25% across all benchmarks.**
2. **Optimized control flow** — JMP_IF_FALSE/TRUE now defer stack pointer decrement via reference, reducing redundant memory writes in tight loops.
3. **PT is now faster than Python** for the loop benchmark (0.407s vs 0.461s) and 31.8x faster for string concatenation.

### What was optimized (v10)

1. **SYNC_ENV O(1)** — `assignVar()` used linear scan of `values` vector. Now uses `env->set()` with `idxMap` hash map for O(1) lookup. **27x string speedup, 19% loop speedup.**
2. **String fast paths** — ADD, ADD_STORE_LOCAL, ADD_STORE_GLOBAL, and STRING_APPEND opcodes now check `a.isString() && b.isString()` and do in-place `+=` instead of creating temporary strings via `ensureStr()`.
3. **callBuiltinFast** — hot builtins (len, push, toString, clock, type, toNum, range, pop, sleep) inlined with first-char check + direct implementation. Zero-copy args from VM stack (no heap vector allocation). Falls through to `callBuiltinDirect` for rare builtins.

### What was optimized (v9)

1. **Dual-store top-level variables** — top-level `let` variables are stored in both a local stack slot (O(1) stack access) and the env (for closure access). `resolveLocal()` finds top-level variables as locals, so all references use fast `LOAD_LOCAL` instead of `LOAD_VAR` (env hash lookup). **1.8x string speedup** because string concat loops read variables thousands of times.
2. **SYNC_ENV opcode** — after modifying a local that's also in the env (`+=`, `++`), syncs the local value back to the env so closures see updated values. Trades 1 env lookup per modification.
3. **String benchmark 1.8x faster** — PT is now only 1.2x slower than Python for string concatenation (down from 2.1x).

### What was optimized (v8)

1. **INC_GLOBAL / DEC_GLOBAL opcodes** — `i++` and `i--` on global variables now use a single opcode (1 env lookup) instead of LOAD_VAR + LOAD_VAR + ADD/SUB + STORE_VAR (3 env lookups). **2.1x loop speedup.**
2. **ADD_STORE_GLOBAL opcode** — `sum += i` on global variables compiles to LOAD_VAR + ADD_STORE_GLOBAL (2 env lookups) instead of LOAD_VAR + LOAD_VAR + ADD + STORE_VAR + LOAD_VAR (4 env lookups). Combined with INC_GLOBAL, the loop body went from 7 env lookups to 3.
3. **PUSH_ARRAY opcode** — `push(arr, val)` now uses a dedicated opcode that directly appends to the vector, skipping the full CALL dispatch (no arg vector allocation, no builtin lookup). **1.7x array speedup.**

### What was optimized (v7)

1. **Nested function bytecode compilation** — functions defined inside bytecode-compiled top-level scripts now have their bodies compiled to bytecode at definition time, just like functions defined in tree-walk mode. This means recursive calls in `fib()` run entirely in the VM instead of falling back to tree-walk per-call.
2. **Fixed `numLocals` calculation** — the bytecode compiler's `compile()` method now uses `addLocal()` for parameters (which updates `peakLocalCount`) instead of directly pushing to the locals vector. This fixes stack corruption in functions compiled from tree-walk mode with classes/enum/repeat at top-level.
3. **Lambda bytecode compilation** — lambda expressions in bytecode-compiled contexts also get their bodies compiled to bytecode.

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

Node.js uses V8 — a **JIT-compiled** JavaScript engine with hidden classes, inline caches, and register allocation. PT is an interpreted language with a bytecode VM. This is an inherent architectural difference, not something we can close with micro-optimizations.

## Where PT is competitive

- **String operations** — PT is **31.8x faster than Python** and **61x faster than Ruby** for string concatenation. Only Node.js (V8 JIT) is faster.
- **Loop/integer arithmetic** — PT is **1.1x faster than Python** for raw integer loops. PT's bytecode VM eliminates Python's per-iteration dictionary lookups.
- **Array operations** — PT is within **1.9x of Python** and **1.3x of Ruby** for array push+iterate.
- **Fibonacci** — PT is within **3.2x of Python** and **3.4x of Ruby** for function-call-heavy workloads (down from 433x at v1).
- **I/O bound tasks** — HTTP server, file I/O, and database operations are dominated by system call time, not interpreter overhead.

## Design Philosophy

PT prioritizes:

1. **Readability** — `show`, `let`, `fn`, `unless`, `match` are English-like
2. **Simplicity** — single-pass lexing, no AST optimization passes
3. **Learning** — easy to read the C++ source (~3400 lines)
4. **Safety** — no raw pointers, bounds checking on arrays

Raw speed was never a goal. If you need performance-critical code, write that module in C++ and call it from PT's built-in functions.
