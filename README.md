# PT Programming Language

A simple, readable programming language implemented in C++17 — built by [Phyo Thant](https://github.com/phyothant-dev) as a learning project in language design and implementation.

**[GitHub Pages](https://phyothant-dev.github.io/PT-Programming-Language/)** — language docs and playground.

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

### Windows (double-click .pt files)

1. Download **PT-Windows.zip** from [Releases](https://github.com/phyothant-dev/PT-Programming-Language/releases)
2. Extract the zip anywhere
3. Right-click **setup.bat** → **Run as administrator**
4. Done!

After that, you can:
- **Double-click** any `.pt` file to run it
- **Right-click** a `.pt` file → "Run with PT"
- Open Command Prompt anywhere and type `pt`
- Or `pt your_program.pt`

To uninstall: run **uninstall.bat** as administrator.

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

## Build a Web Server

PT has a **built-in HTTP server**. Create a file and run it:

```pt
// server.pt
let page = readFile("index.html");

httpListen(3000, (req) => {
  show(req.method + " " + req.path);

  if (req.path is "/") {
    return page;
  }

  if (req.path is "/api/hello") {
    return {status: 200, headers: {"content-type": "application/json"}, body: toJSON({hello: "world"})};
  }

  return {status: 404, body: "<h1>404</h1>"};
});
```

```sh
pt server.pt
# Server running at http://localhost:3000
```

### Request object

| Property | Description |
|----------|-------------|
| `req.method` | `"GET"`, `"POST"`, etc. |
| `req.path` | `"/about"`, `"/api/users"` |
| `req.headers` | Map of HTTP headers |
| `req.body` | Request body (POST data) |

### Response formats

```pt
// Simple HTML response
return "<h1>Hello!</h1>";

// JSON API response
return {status: 200, headers: {"content-type": "application/json"}, body: toJSON({key: "value"})};
```

### Deploy with systemd (Linux)

```sh
# Create service file
sudo tee /etc/systemd/system/pt-server.service <<EOF
[Unit]
Description=PT Web Server
After=network.target

[Service]
ExecStart=/usr/local/bin/pt /path/to/server.pt
Restart=always

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl enable pt-server
sudo systemctl start pt-server
```

---

## Features

### Core

- **Variables** — `let name = value;` (also `var` for compatibility)
- **Constants** — `const PI = 3.14;` (cannot be reassigned)
- **Compound assignment** — `+=`, `-=`, `*=`, `/=`, `%=`
- **Arithmetic** — `+`, `-`, `*`, `/`, `%` with proper precedence
- **Postfix** — `i++`, `i--`
- **String repetition** — `"ha" * 3` → `"hahaha"`, `3 * "ab"` → `"ababab"`
- **Comparisons** — `is`, `isnt`, `<`, `<=`, `>`, `>=`
- **Logical operators** — `and`, `or`, `not` with short-circuit evaluation
- **In operator** — `20 in [10, 20, 30]`, `"a" in "apple"`, `"key" in map`

### Strings

- **String types** — double-quoted `"hello"`, backtick multiline `` `line1\nline2` ``
- **String interpolation** — `"Hello, ${name}!"`
- **String builtins** — `upper()`, `lower()`, `trim()`, `substr()`, `contains()`, `replace()`, `split()`

### Control Flow

- **If/elif/else** — `if (cond) { ... } elif (cond) { ... } else { ... }`
- **Unless** — `unless (cond) { ... }` (opposite of `if`)
- **Ternary operator** — `condition ? trueVal : falseVal`
- **While loops** — `while (cond) { ... }`
- **Loop** — `loop { ... }` (infinite loop, use `break` to exit)
- **Repeat** — `repeat 5 { ... }` (run block N times)
- **For loops** — `for (let i = 0; i < n; i += 1) { ... }`
- **For-each loops** — `for (item in arr) { ... }`
- **Match expressions** — `match(val) { 1 => "one" 2 => "two" _ => "other" }`
- **Break/Continue** — loop control

### Functions & Lambdas

- **Functions** — `fn name(params) { ... }` with `return`
- **Default params** — `fn greet(name = "World") { ... }`
- **Arrow functions** — `fn name(params) => expression;` or inline `(x) => x * 2`
- **Closures** — functions capture enclosing scope, mutable state
- **Recursion** — fully supported
- **Lexical scoping** — blocks create new scopes, inner shadows outer

### Collections

- **Arrays** — `[1, 2, 3]`, index read/write `arr[0] = val`, negative indexing `arr[-1]`
- **Maps/Dictionaries** — `{name: "PT", ver: 2}`, dot access `obj.key`, bracket access `obj["key"]`
- **List comprehensions** — `[x * 2 for x in nums]`, `[x for x in nums if x > 3]`
- **Map builtins** — `keys()`, `values()`, `has()`
- **Higher-order builtins** — `map()`, `filter()`, `reduce()`
- **Collection builtins** — `join()`, `indexOf()`, `sort()`, `range()`

### Object-Oriented Programming

- **Classes** — `class Dog { ... }`
- **Constructor** — `fn init(name) { this.name = name; }`
- **Inheritance** — `class Puppy < Dog { ... }`
- **This/Super** — `this.name`, `super.method()`
- **Static methods** — `static fn create() { ... }`
- **Enums** — `enum Color { RED, GREEN, BLUE }` (RED=0, GREEN=1, BLUE=2)

### Web & I/O

- **HTTP server** — `httpListen(port, handler)` with POSIX sockets
- **HTTP client** — `httpGet(url)`, `httpPost(url, body)`, `httpPut(url, body)`, `httpDelete(url)`
- **JSON** — `parseJSON(str)` → maps/arrays, `toJSON(val)` → JSON string with optional pretty-print
- **File I/O** — `readFile(path)`, `writeFile(path, content)`, `fileExists(path)`
- **Environment** — `getenv("PORT")` to read environment variables
- **Imports** — `import "module.pt";` or `import "module.pt" as mod;`

### Crypto & Auth

- **Hashing** — `hash(str)` (SHA-256), `hash(str, "md5")` (MD5)
- **Base64** — `base64Encode(str)`, `base64Decode(str)`
- **UUID** — `uuid()` generates a v4 UUID

### Concurrency

- **Spawn** — `spawn(fn, args...)` runs function in a background thread
- **Sleep** — `sleep(ms)` pauses execution for N milliseconds

### Other

- **Math builtins** — `abs()`, `sqrt()`, `min()`, `max()`, `floor()`, `ceil()`, `round()`
- **Type checking** — `type(val)` returns `"number"`, `"string"`, `"array"`, `"map"`, `"function"`, `"class"`, or `"nil"`
- **Error handling** — `try { ... } catch (e) { ... } finally { ... }`, `throw "message"`
- **Print** — `show(val)` prints with newline, `print(val)` prints without newline
- **Assert** — `assert(cond, msg)` — throws on failure
- **Random** — `random()` (0-1), `random(100)` (0-99), `random(1, 10)` (1-9)
- **Clock** — `clock()` returns seconds since epoch
- **Comments** — `// line comments`
- **REPL** — interactive mode with multi-line support
- **Export** — `export fn name() { ... }` for modules

---

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
x++;                // postfix increment
```

### Conditionals

```pt
let grade = 85;
if (grade >= 90) {
  show("A");
} elif (grade >= 80) {
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

// repeat
repeat 5 { show("hello!"); }

// for
for (let n = 0; n < 5; n += 1) { show(n); }

// for-each
for (item in [1, 2, 3]) { show(item); }
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

// list comprehension
let squares = [x * x for x in nums];
let big = [x for x in nums if x > 3];
```

### Pattern Matching

```pt
let day = 3;
let name = match(day) {
  1 => "Monday"
  2 => "Tuesday"
  3 => "Wednesday"
  _ => "Other"
};
show(name);  // Wednesday

let x = 42;
match (x) {
  0 => show("zero")
  42 => show("the answer!")
  _ => show("something else")
}
```

### Classes & OOP

```pt
class Animal {
  name = "";
  sound = "";

  fn init(name, sound) {
    this.name = name;
    this.sound = sound;
  }

  fn speak() {
    return this.name + " says " + this.sound + "!";
  }
}

class Dog < Animal {
  fn init(name) {
    this.name = name;
    this.sound = "woof";
  }
}

let rex = Dog("Rex");
show(rex.speak());  // Rex says woof!

// enums
enum Color { RED, GREEN, BLUE }
show(Color.RED);    // 0
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
let person = {name: "PT", version: 2};
show(person.name);       // dot access
show(person["version"]); // bracket access
person.author = "Phyo";  // dot assignment
show(keys(person));      // [name, version, author]
show(has(person, "name")); // true
```

### HTTP Server

```pt
let page = readFile("index.html");

httpListen(3000, (req) => {
  show(req.method + " " + req.path);

  if (req.path is "/") return page;

  if (req.path is "/api/hello") {
    return {status: 200, headers: {"content-type": "application/json"}, body: toJSON({hello: "world"})};
  }

  return {status: 404, body: "<h1>404</h1>"};
});
```

### Full-Stack Web App

```pt
import "auth.pt" as auth;

let db = sqliteOpen("app.db");
sqliteExec(db, "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, email TEXT)");

httpListen(3000, (req) => {
  if (req.path is "/api/users" && req.method is "GET") {
    let users = sqliteQuery(db, "SELECT * FROM users");
    return {status: 200, headers: {"content-type": "application/json"}, body: toJSON(users)};
  }

  if (req.path is "/api/users" && req.method is "POST") {
    let data = parseJSON(req.body);
    sqliteExec(db, "INSERT INTO users (name, email) VALUES ('" + data.name + "', '" + data.email + "')");
    return {status: 201, body: toJSON({ok: true, id: uuid()})};
  }

  return {status: 404, body: toJSON({error: "not found"})};
});
```

---

## Built-in Functions

### Core
| Function | Description |
|----------|-------------|
| `len(s)` | Length of string or array |
| `push(arr, val)` | Append to array |
| `pop(arr)` | Remove and return last element |
| `toNum(s)` | Convert string to number |
| `toString(v)` | Convert value to string |
| `type(v)` | Return type name as string |
| `assert(cond, msg)` | Throw if condition is false |
| `clock()` | Seconds since epoch |
| `random()` | Random 0.0–1.0 |
| `random(max)` | Random 0 to max-1 |
| `random(min, max)` | Random min to max-1 |
| `getenv(key)` | Read environment variable |
| `input(prompt)` | Read line from stdin |

### String
| Function | Description |
|----------|-------------|
| `upper(s)` | Uppercase |
| `lower(s)` | Lowercase |
| `trim(s)` | Trim whitespace |
| `substr(s, start, len)` | Substring |
| `contains(s, sub)` | Check if contains |
| `replace(s, old, new)` | Replace all |
| `split(s, delim)` | Split into array |
| `join(arr, delim)` | Join array |
| `indexOf(s, val)` | Find index |

### Math
| Function | Description |
|----------|-------------|
| `abs(x)` | Absolute value |
| `sqrt(x)` | Square root |
| `min(a, b)` | Minimum |
| `max(a, b)` | Maximum |
| `floor(x)` | Floor |
| `ceil(x)` | Ceil |
| `round(x)` | Round |

### Collections
| Function | Description |
|----------|-------------|
| `sort(arr)` | Sorted copy |
| `range(end)` | [0..end-1] |
| `range(start, end)` | [start..end-1] |
| `map(arr, fn)` | Transform elements |
| `filter(arr, fn)` | Filter elements |
| `reduce(arr, fn, init)` | Reduce to single value |
| `keys(map)` | Array of keys |
| `values(map)` | Array of values |
| `has(map, key)` | Check if key exists |

### File & Web
| Function | Description |
|----------|-------------|
| `readFile(path)` | Read file contents |
| `writeFile(path, content)` | Write to file |
| `fileExists(path)` | Check if file exists |
| `httpListen(port, handler)` | Start HTTP server |
| `httpGet(url)` | HTTP GET request |
| `httpPost(url, body)` | HTTP POST request |
| `httpPut(url, body)` | HTTP PUT request |
| `httpDelete(url)` | HTTP DELETE request |
| `parseJSON(str)` | Parse JSON string to map/array |
| `toJSON(val)` | Serialize to JSON string |

### Crypto & Auth
| Function | Description |
|----------|-------------|
| `hash(str)` | SHA-256 hash |
| `hash(str, "md5")` | MD5 hash |
| `base64Encode(str)` | Base64 encode |
| `base64Decode(str)` | Base64 decode |
| `uuid()` | Generate v4 UUID |

### Concurrency
| Function | Description |
|----------|-------------|
| `spawn(fn, args...)` | Run function in background thread |
| `sleep(ms)` | Pause execution for N milliseconds |

### Database
| Function | Description |
|----------|-------------|
| `sqliteOpen(path)` | Open SQLite database |
| `sqliteExec(db, sql)` | Execute SQL (no result) |
| `sqliteQuery(db, sql)` | Execute SQL query (returns rows) |
| `sqliteClose(db)` | Close database |
| `pgOpen(connStr)` | Open PostgreSQL connection |
| `pgExec(db, sql)` | Execute SQL (no result) |
| `pgQuery(db, sql)` | Execute SQL query (returns rows) |
| `pgClose(db)` | Close connection |

---

## Project Structure

```
PT-Programming-Language/
├── pt                    # compiled binary
├── Makefile              # build system
├── install.sh            # one-line installer
├── server.pt             # web demo server (localhost:3000)
├── test.pt               # test suite (186 tests)
├── README.md
├── .gitignore
├── docs/                 # GitHub Pages website
│   ├── index.html        # landing page
│   ├── style.css         # dark theme CSS
│   ├── docs.html         # language reference
│   ├── playground.html   # interactive examples
│   └── logo.svg          # project logo
├── src/
│   ├── main.cpp          # entry point, REPL, file runner
│   ├── token.h           # token type definitions
│   ├── lexer.h/.cpp      # scanner — source → tokens
│   ├── ast.h             # AST node definitions
│   ├── parser.h/.cpp     # parser — tokens → AST
│   ├── http.h/.cpp       # HTTP server (POSIX sockets)
│   ├── interpreter.h/.cpp# tree-walk interpreter + bytecode VM
│   ├── json.h/.cpp       # JSON parser/serializer
│   ├── ptcurl.h/.cpp     # HTTP client (libcurl)
│   ├── crypto.h/.cpp     # SHA-256, MD5, Base64, UUID
│   └── pg.h/.cpp         # PostgreSQL driver (optional)
├── bench/                # performance benchmarks
│   ├── COMPARISON.md     # language comparison
│   ├── fib.pt            # recursive fibonacci
│   ├── loop.pt           # loop sum 10M
│   ├── string.pt         # string concat 100K
│   ├── array.pt          # array push 100K
│   └── *.py / *.js / *.rb  # reference implementations
├── examples/             # example programs
│   ├── api/              # REST API
│   ├── blog/             # blog engine
│   ├── chat/             # real-time chat
│   ├── dashboard/        # dashboard app
│   ├── portfolio/        # portfolio site
│   └── todo/             # todo app
└── vscode-pt/            # VS Code extension
```

---

## Requirements

- **C++17 compiler** — g++ 7+ or clang++ 5+
- **Git** — to clone the repository
- **libcurl** — for HTTP client (`httpGet`, `httpPost`, etc.)
- **libsqlite3** — for SQLite database support
- **libpq** (optional) — for PostgreSQL support, build with `make pg`

No other dependencies. No package managers. No runtime required.

Windows users can skip all requirements — just download the pre-built `.exe` from [Releases](https://github.com/phyothant-dev/PT-Programming-Language/releases).

---

## Performance

PT includes a bytecode VM with optimizations for numeric operations. Here's how it compares:

| Benchmark | PT v10 | Python 3 | Ruby | Node.js |
|-----------|--------|----------|------|---------|
| fib(30) recursive | **0.22s** | 0.053s | 0.047s | 0.005s |
| Loop 10M | **0.491s** | 0.075s | ~0s | 0.009s |
| Array 100K | **0.018s** | 0.002s | 0.003s | 0.002s |
| String 100K | **0.005s** | 0.117s | 0.218s | 0.003s |

PT is **129x faster** than v1 for recursive workloads, **41x faster** for loops, and **126x faster** for string operations. PT is now **23x faster than Python** for string concatenation. Full benchmark details in [bench/COMPARISON.md](bench/COMPARISON.md).

---

## License

MIT
