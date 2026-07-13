# PT Programming Language

A simple, readable programming language implemented in C++17 — built by [Phyo Thant](https://github.com/phyothant-dev) as a learning project in language design and implementation.

---

## Installation

### macOS

**Option A: One-line install (recommended)**

Open Terminal and run:

```sh
curl -sSL https://raw.githubusercontent.com/phyothant-dev/PT-Programming-Language/main/install.sh | sh
```

**Option B: Install with make**

```sh
# 1. Install Xcode Command Line Tools (if not installed)
xcode-select --install

# 2. Clone and build
git clone https://github.com/phyothant-dev/PT-Programming-Language.git
cd PT-Programming-Language
make

# 3. Install to /usr/local/bin (optional, so you can run 'pt' from anywhere)
make install
```

### Linux (Ubuntu / Debian)

```sh
# 1. Install build tools
sudo apt update
sudo apt install -y git g++

# 2. One-line install
curl -sSL https://raw.githubusercontent.com/phyothant-dev/PT-Programming-Language/main/install.sh | sh
```

### Linux (Fedora / RHEL / CentOS)

```sh
# 1. Install build tools
sudo dnf install -y git gcc-c++

# 2. One-line install
curl -sSL https://raw.githubusercontent.com/phyothant-dev/PT-Programming-Language/main/install.sh | sh
```

### Linux (Arch / Manjaro)

```sh
# 1. Install build tools
sudo pacman -S git gcc

# 2. One-line install
curl -sSL https://raw.githubusercontent.com/phyothant-dev/PT-Programming-Language/main/install.sh | sh
```

### Windows (no WSL needed)

Download the pre-built Windows binary from [Releases](https://github.com/phyothant-dev/PT-Programming-Language/releases), extract the `.zip`, then:

```cmd
pt.exe hello.pt
```

Or use in Command Prompt / PowerShell directly:

```cmd
pt.exe
>> show("Hello from Windows!");
Hello from Windows!
>> exit
```

No compiler, no WSL, no extra software needed. Just download and run.

---

## Quick Start

```sh
# Start the REPL (interactive mode)
pt

# Run a file
pt myfile.pt
```

### Your first program

Create a file called `hello.pt`:

```pt
show("Hello, World!");
```

Run it:

```sh
pt hello.pt
```

### Interactive REPL

```sh
pt
>> let name = "World";
>> show("Hello, " + name + "!");
Hello, World!
>> 2 + 3
5
>> exit
```

---

## Features

- **Variables** — `let name = value;` (also `var` for compatibility)
- **Constants** — `const PI = 3.14;` (cannot be reassigned)
- **Compound assignment** — `+=`, `-=`, `*=`, `/=`, `%=`
- **Arithmetic** — `+`, `-`, `*`, `/`, `%` with proper precedence
- **Strings** — double-quoted, concatenation with `+`, indexing `s[0]`
- **String interpolation** — `"Hello, ${name}!"`
- **String builtins** — `upper()`, `lower()`, `trim()`, `substr()`, `contains()`, `replace()`, `split()`
- **Comparisons** — `is`, `isnt`, `<`, `<=`, `>`, `>=`
- **Logical operators** — `and`, `or` with short-circuit evaluation
- **If/else/else if** — `if (cond) { ... } else if (cond) { ... } else { ... }`
- **Unless** — `unless (cond) { ... }` (opposite of `if`)
- **Ternary operator** — `condition ? trueVal : falseVal`
- **While loops** — `while (cond) { ... }`
- **Loop** — `loop { ... }` (infinite loop, use `break` to exit)
- **For loops** — `for (let i = 0; i < n; i += 1) { ... }`
- **For-each loops** — `for (item in arr) { ... }`
- **Break/Continue** — loop control
- **Functions** — `fn name(params) { ... }` with `return`
- **Arrow functions** — `fn name(params) => expression;` or inline `(x) => x * 2`
- **Closures** — functions capture enclosing scope, mutable state
- **Recursion** — fully supported
- **Lexical scoping** — blocks create new scopes, inner shadows outer
- **Arrays** — `[1, 2, 3]`, index read/write `arr[0] = val`, negative indexing `arr[-1]`
- **Maps/Dictionaries** — `{"key": val}`, dot access `obj.key`, bracket access `obj["key"]`
- **Map builtins** — `keys()`, `values()`, `has()`
- **Math builtins** — `abs()`, `sqrt()`, `min()`, `max()`, `floor()`, `ceil()`, `round()`
- **Higher-order builtins** — `map()`, `filter()`, `reduce()`
- **Collection builtins** — `join()`, `indexOf()`, `sort()`, `range()`
- **Type checking** — `type(val)` returns `"number"`, `"string"`, `"array"`, `"map"`, `"function"`, or `"nil"`
- **Error handling** — `try { ... } catch (e) { ... } finally { ... }`, `throw "message"`
- **Imports** — `import "module.pt";` or `import "module.pt" as mod;`
- **File I/O** — `readFile(path)`, `writeFile(path, content)`
- **Built-in functions** — `len()`, `push()`, `pop()`, `toNum()`, `toString()`, `input()`, `clock()`, `random()`
- **Print** — `show(val)` prints with newline, `print(val)` prints without newline
- **Assert** — `assert(cond, msg)` — throws on failure
- **Comments** — `// line comments`
- **REPL** — interactive mode with multi-line support
- **File execution** — run `.pt` files

## Examples

### Hello World

```pt
show("Hello, World!");
```

### Variables & Constants

```pt
let x = 10;
var y = 3;          // var works too
const PI = 3.14159;
x += 5;             // compound assignment
```

### Conditionals

```pt
let grade = 85;
if (grade >= 90) {
  show("A");
} else if (grade >= 80) {
  show("B");
} else {
  show("C");
}

unless (x is y) {
  show("different");
}

let msg = x > y ? "x wins" : "y wins";
```

### Loops

```pt
// while
let i = 0;
while (i < 5) { i += 1; }

// loop (infinite with break)
let count = 0;
loop {
  count += 1;
  if (count is 5) break;
}

// for
for (let n = 0; n < 5; n += 1) { show(n); }

// for-each
for (item in [1, 2, 3]) { show(item); }

// for-each on strings
for (c in "hello") { print(c); }
```

### Functions & Lambdas

```pt
fn fact(n) {
  if (n <= 1) return 1;
  return n * fact(n - 1);
}
show(fact(6));  // 720

// arrow functions
fn square(n) => n * n;
show(square(5));  // 25

// inline lambdas
let add = (a, b) => a + b;
show(add(3, 4));  // 7

// higher-order functions
let nums = [1, 2, 3, 4, 5];
let doubled = map(nums, (x) => x * 2);
let evens = filter(nums, (x) => x % 2 == 0);
let total = reduce(nums, (a, b) => a + b, 0);
```

### Closures

```pt
fn makeCounter() {
  let count = 0;
  return () => {
    count += 1;
    return count;
  };
}
let c = makeCounter();
show(c());  // 1
show(c());  // 2
```

### Maps

```pt
let person = {"name": "PT", "version": 2};
show(person.name);       // dot access
show(person["version"]); // bracket access
person.author = "Phyo";  // dot assignment
show(keys(person));      // ["name", "version", "author"]
show(has(person, "name")); // true
```

### String Interpolation

```pt
let name = "World";
show("Hello, ${name}!");
show("2 + 3 = ${2 + 3}");
```

### Error Handling

```pt
try {
  let result = riskyOperation();
} catch (e) {
  show("Error: " + e);
} finally {
  cleanup();
}

throw "something went wrong";
```

### Imports

```pt
// math.pt defines add, subtract, PI
import "math.pt";
show(add(1, 2));

// or with alias
import "math.pt" as math;
show(math.PI);
```

### Built-in Functions

```pt
// Core
len("hello")              // 5
len([1, 2, 3])            // 3
push(arr, val)            // append to array
pop(arr)                  // remove and return last element
toNum("42")               // 42
toString(42)              // "42"
type(42)                  // "number"
assert(1 == 1, "error")  // throws if false
clock()                   // seconds since epoch
random()                  // 0.0 to 1.0
random(100)               // 0 to 99
random(1, 10)             // 1 to 9

// String
upper("hello")            // "HELLO"
lower("HELLO")            // "hello"
trim("  hi  ")            // "hi"
substr("hello", 1, 3)     // "ell"
contains("hello", "ell")  // true
replace("hello", "l", "r") // "herro"
split("a,b,c", ",")       // ["a", "b", "c"]
join(["a", "b"], "-")     // "a-b"
indexOf("hello", "ell")   // 1

// Math
abs(-42)                  // 42
sqrt(9)                   // 3
min(3, 7)                 // 3
max(3, 7)                 // 7
floor(3.7)                // 3
ceil(3.2)                 // 4
round(3.5)                // 4

// Collections
sort([3, 1, 2])           // [1, 2, 3]
range(5)                  // [0, 1, 2, 3, 4]
range(1, 6)               // [1, 2, 3, 4, 5]
map([1, 2], (x) => x * 2) // [2, 4]
filter([1, 2, 3], (x) => x > 1) // [2, 3]
reduce([1, 2, 3], (a, b) => a + b, 0) // 6

// Maps
keys({"a": 1, "b": 2})    // ["a", "b"]
values({"a": 1, "b": 2})  // [1, 2]
has({"a": 1}, "a")         // true

// File I/O
readFile(path)            // read file contents (nil on error)
writeFile(path, content)  // write to file (true/false)

// Input
input("name: ")           // reads a line from stdin
```

## Project Structure

```
PT-Programming-Language/
├── pt                  # compiled binary
├── Makefile            # build system
├── install.sh          # one-line installer
├── test.pt             # test suite (113 tests)
├── README.md
├── .gitignore
└── src/
    ├── main.cpp        # entry point, REPL, file runner
    ├── token.h         # token type definitions
    ├── lexer.h/.cpp    # scanner — source → tokens
    ├── ast.h           # AST node definitions
    ├── parser.h/.cpp   # parser — tokens → AST
    └── interpreter.h/.cpp  # tree-walk interpreter
```

## Requirements

- **C++17 compiler** — g++ 7+ or clang++ 5+
- **Git** — to clone the repository

No other dependencies. No package managers. No runtime required.

Windows users can skip all requirements — just download the pre-built `.exe` from [Releases](https://github.com/phyothant-dev/PT-Programming-Language/releases).

## Grammar

```
program      → declaration* EOF
declaration  → constDecl | funDecl | varDecl | statement
constDecl    → "const" IDENTIFIER "=" expression ";"
funDecl      → "fn" IDENTIFIER "(" parameters? ")" ( "=>" expression ";" | block )
varDecl      → ("let" | "var") IDENTIFIER ("=" expression)? ";"
statement    → exprStmt | showStmt | printStmt | block | ifStmt | unlessStmt
             | whileStmt | loopStmt | forStmt | breakStmt | continueStmt
             | returnStmt | tryStmt | importStmt
exprStmt     → expression ";"
showStmt     → "show" expression ";"
printStmt    → "print" expression ";"
block        → "{" declaration* "}"
ifStmt       → "if" "(" expression ")" statement ("else if" "(" expression ")" statement)* ("else" statement)?
unlessStmt   → "unless" "(" expression ")" statement
whileStmt    → "while" "(" expression ")" statement
loopStmt     → "loop" statement
forStmt      → "for" "(" (varDecl | exprStmt | ";") expression? ";" expression? ")" statement
             | "for" "(" IDENTIFIER "in" expression ")" statement
breakStmt    → "break" ";"
continueStmt → "continue" ";"
returnStmt   → "return" expression? ";"
tryStmt      → "try" block ("catch" ("(" IDENTIFIER ")")? block)? ("finally" block)?
importStmt   → "import" STRING ("as" IDENTIFIER)? ";"
expression   → assignment
assignment   → IDENTIFIER ("+=" | "-=" | "*=" | "/=" | "%=") assignment
             | IDENTIFIER "=" assignment | IDENTIFIER "[" expression "]" "=" assignment
             | IDENTIFIER "." IDENTIFIER "=" assignment | ternary
ternary      → or ("?" expression ":" assignment)?
or           → and ("or" and)*
and          → equality ("and" and)*
equality     → comparison (("is" | "isnt" | "==" | "!=") comparison)*
comparison   → term (("<" | "<=" | ">" | ">=") term)*
term         → factor (("+" | "-") factor)*
factor       → unary (("*" | "/" | "%") unary)*
unary        → ("!" | "-" | "throw") unary | call
call         → primary ( "(" arguments? ")" | "[" expression "]" | "." IDENTIFIER )*
primary      → NUMBER | STRING | "true" | "false" | "nil"
             | IDENTIFIER | "(" expression ")" | "[" (expression ("," expression)*)? "]"
             | "{" (expression ":" expression ("," expression ":")*)? "}"
             | "(" parameters? ")" "=>" (expression | block)
```

## License

MIT
