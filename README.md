# PT Programming Language

A simple, readable programming language implemented in C++17 — built by [Phyo Thant](https://github.com/phyothant-dev) as a learning project in language design and implementation.

## Features

- **Variables** — `let name = value;`
- **Compound assignment** — `+=`, `-=`, `*=`, `/=`, `%=`
- **Arithmetic** — `+`, `-`, `*`, `/`, `%` with proper precedence
- **Strings** — double-quoted, concatenation with `+`
- **Comparisons** — `is`, `isnt`, `<`, `<=`, `>`, `>=`
- **Logical operators** — `and`, `or` with short-circuit evaluation
- **If/else** — `if (cond) { ... } else { ... }`
- **Unless** — `unless (cond) { ... }` (opposite of `if`)
- **While loops** — `while (cond) { ... }`
- **Loop** — `loop { ... }` (infinite loop, use `break` to exit)
- **For loops** — `for (let i = 0; i < n; i = i + 1) { ... }`
- **For-each loops** — `for (item in arr) { ... }`
- **Ternary operator** — `condition ? trueVal : falseVal`
- **Break/Continue** — loop control
- **Assert** — `assert(cond, msg)` — throws on failure
- **Functions** — `fn name(params) { ... }` with `return`
- **Arrow functions** — `fn name(params) => expression;`
- **Closures** — functions capture enclosing scope, mutable state
- **Recursion** — fully supported
- **Lexical scoping** — blocks create new scopes, inner shadows outer
- **Arrays** — `[1, 2, 3]`, index read/write `arr[0] = val`, negative indexing `arr[-1]`, nested arrays
- **String indexing** — `str[0]` returns a single character, `str[-1]` returns last character
- **String builtins** — `upper()`, `lower()`, `trim()`, `substr()`, `contains()`, `replace()`, `split()`
- **Math builtins** — `abs()`, `sqrt()`, `min()`, `max()`, `floor()`, `ceil()`, `round()`
- **Type checking** — `type(val)` returns `"number"`, `"string"`, `"array"`, `"function"`, or `"nil"`
- **File I/O** — `readFile(path)`, `writeFile(path, content)`
- **Built-in functions** — `len()`, `push()`, `pop()`, `toNum()`, `toString()`, `input()`
- **Comments** — `// line comments`
- **REPL** — interactive mode
- **File execution** — run `.pt` files

## Quick Start

```sh
# Build
g++ -std=c++17 -O2 -o pt src/main.cpp src/lexer.cpp src/parser.cpp src/interpreter.cpp

# Run a file
./pt test.pt

# REPL mode
./pt
>> fn hello(name) {
.. print("Hello, " + name + "!");
.. }
>> hello("world");
Hello, world!
>> exit
```

## Examples

### Hello World

```pt
print "hello world";
```

### Variables & Arithmetic

```pt
let x = 10;
let y = 3;
print x + y;     // 13
print x * y;     // 30
print (x - y) / 2;  // 3.5
print x % y;     // 1

// compound assignment
x += 5;   // x = 15
x -= 3;   // x = 12
x *= 2;   // x = 24
x /= 4;   // x = 6
x %= 4;   // x = 2
```

### Conditionals

```pt
if (x > y) {
  print "x is bigger";
} else {
  print "nope";
}

// unless (opposite of if)
unless (x is y) {
  print "x and y are different";
}

// ternary
let msg = x > y ? "x wins" : "y wins";
```

### Loops

```pt
// while
let i = 0;
while (i < 5) {
  print i;
  i = i + 1;
}

// loop (infinite loop with break)
let count = 0;
loop {
  count += 1;
  if (count is 5) break;
}

// for
for (let n = 0; n < 5; n = n + 1) {
  print n;
}

// for-each
for (item in [1, 2, 3]) {
  print item;
}

// for-each on strings
for (c in "hello") {
  print c;
}

// break / continue
let a = 0;
while (a < 10) {
  if (a is 3) break;
  if (a is 1) { a = a + 1; continue; }
  print a;
  a = a + 1;
}
```

### Functions & Recursion

```pt
fn fact(n) {
  if (n <= 1) return 1;
  return n * fact(n - 1);
}
print fact(6);  // 720

// arrow functions
fn add = (a, b) => a + b;
print add(3, 4);  // 7

fn square(n) => n * n;
print square(5);  // 25
```

### Closures

```pt
fn makeCounter() {
  let count = 0;
  fn counter() {
    count = count + 1;
    return count;
  }
  return counter;
}

let c = makeCounter();
print c();  // 1
print c();  // 2
print c();  // 3
```

### Scope

```pt
let x = "outer";
{
  print x;    // outer
  let x = "inner";
  print x;    // inner
}
print x;      // outer
```

### Arrays

```pt
let arr = [1, 2, 3, 4, 5];
print arr;        // [1, 2, 3, 4, 5]
print arr[0];     // 1
print arr[-1];    // 5 (negative indexing)
arr[2] = 99;      // [1, 2, 99, 4, 5]
push(arr, 6);     // [1, 2, 99, 4, 5, 6]
print pop(arr);   // 6
print len(arr);   // 5

// nested arrays
let nested = [[1, 2], [3, 4]];
print nested[0][1];  // 2
```

### Strings & Type Checking

```pt
let s = "hello";
print s[0];      // h
print s[-1];     // o (negative indexing)
print len(s);    // 5

print type(42);      // number
print type("hi");    // string
print type([1, 2]);  // array
print type(nil);     // nil
```

### Math Built-ins

```pt
print abs(-42);      // 42
print sqrt(9);       // 3
print min(3, 7);     // 3
print max(3, 7);     // 7
print floor(3.7);    // 3
print ceil(3.2);     // 4
print round(3.5);    // 4
```

### File I/O

```pt
writeFile("/tmp/data.txt", "hello world");
print readFile("/tmp/data.txt");  // hello world
print readFile("/tmp/nope");      // nil (file doesn't exist)
```

### Built-in Functions

```pt
// Core
len("hello")              // 5
len([1, 2, 3])            // 3
push(arr, val)            // append to array
pop(arr)                  // remove and return last element
toNum("42")               // 42
toNum("abc")              // nil
toString(42)              // "42"
input("name: ")           // reads a line from stdin
type(42)                  // "number"
assert(1 == 1, "error")  // throws if false

// String
upper("hello")            // "HELLO"
lower("HELLO")            // "hello"
trim("  hi  ")            // "hi"
substr("hello", 1, 3)     // "ell"
contains("hello", "ell")  // true
replace("hello", "l", "r") // "herro"
split("a,b,c", ",")       // ["a", "b", "c"]

// Math
abs(-42)                  // 42
sqrt(9)                   // 3
min(3, 7)                 // 3
max(3, 7)                 // 7
floor(3.7)                // 3
ceil(3.2)                 // 4
round(3.5)                // 4

// File I/O
readFile(path)            // read file contents (nil on error)
writeFile(path, content)  // write to file (true/false)
```

### Logical Operators

```pt
true and false   // false
true or false    // true
(10 > 5) and (20 > 10)  // true
```

## Project Structure

```
pt/
├── pt                  # compiled binary
├── test.pt             # test program
├── src/
│   ├── main.cpp        # entry point, REPL, file runner
│   ├── token.h         # token type definitions
│   ├── lexer.h/.cpp    # scanner — source → tokens
│   ├── ast.h           # AST node definitions
│   ├── parser.h/.cpp   # parser — tokens → AST
│   └── interpreter.h/.cpp  # tree-walk interpreter
```

## Build

Requires a C++17 compiler (g++ or clang++).

```sh
g++ -std=c++17 -O2 -o pt src/main.cpp src/lexer.cpp src/parser.cpp src/interpreter.cpp
```

## Grammar

```
program      → declaration* EOF
declaration  → funDecl | varDecl | statement
funDecl      → "fn" IDENTIFIER "(" parameters? ")" ( "->" expression ";" | block )
varDecl      → ("let" | "var") IDENTIFIER ("=" expression)? ";"
statement    → exprStmt | printStmt | block | ifStmt | unlessStmt | whileStmt
             | loopStmt | forStmt | breakStmt | continueStmt | returnStmt
exprStmt     → expression ";"
printStmt    → "print" expression ";"
block        → "{" declaration* "}"
ifStmt       → "if" "(" expression ")" statement ("else" statement)?
unlessStmt   → "unless" "(" expression ")" statement
whileStmt    → "while" "(" expression ")" statement
loopStmt     → "loop" statement
forStmt      → "for" "(" (varDecl | exprStmt | ";") expression? ";" expression? ")" statement
             | "for" "(" IDENTIFIER "in" expression ")" statement
breakStmt    → "break" ";"
continueStmt → "continue" ";"
returnStmt   → "return" expression? ";"
expression   → assignment
assignment   → IDENTIFIER ("+=" | "-=" | "*=" | "/=" | "%=") assignment
             | IDENTIFIER "=" assignment | IDENTIFIER "[" expression "]" "=" assignment | ternary
ternary      → or ("?" expression ":" assignment)?
or           → and ("or" and)*
and          → equality ("and" and)*
equality     → comparison (("is" | "isnt" | "==" | "!=") comparison)*
comparison   → term (("<" | "<=" | ">" | ">=") term)*
term         → factor (("+" | "-") factor)*
factor       → unary (("*" | "/" | "%") unary)*
unary        → ("!" | "-") unary | call
call         → primary ( "(" arguments? ")" | "[" expression "]" )*
primary      → NUMBER | STRING | "true" | "false" | "nil"
             | IDENTIFIER | "(" expression ")" | "[" (expression ("," expression)*)? "]"
```

## License

MIT
