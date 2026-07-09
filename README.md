# PT Programming Language

A simple, readable programming language implemented in C++17 — built by [Phyo Thant](https://github.com/phyothant-dev) as a learning project in language design and implementation.

## Features

- **Variables** — `var name = value;`
- **Arithmetic** — `+`, `-`, `*`, `/` with proper precedence
- **Strings** — double-quoted, concatenation with `+`
- **Comparisons** — `==`, `!=`, `<`, `<=`, `>`, `>=`
- **Logical operators** — `and`, `or` with short-circuit evaluation
- **If/else** — `if (cond) { ... } else { ... }`
- **While loops** — `while (cond) { ... }`
- **For loops** — `for (var i = 0; i < n; i = i + 1) { ... }`
- **Break/Continue** — loop control
- **Functions** — `fun name(params) { ... }` with `return`
- **Closures** — functions capture enclosing scope, mutable state
- **Recursion** — fully supported
- **Lexical scoping** — blocks create new scopes, inner shadows outer
- **Comments** — `// line comments`
- **Built-in functions** — `len()`, `toNum()`, `toString()`, `input()`
- **REPL** — interactive mode
- **File execution** — run `.pt` files

## Quick Start

```sh
# Build
g++ -std=c++17 -o pt src/main.cpp src/lexer.cpp src/parser.cpp src/interpreter.cpp

# Run a file
./pt test.pt

# REPL mode
./pt
> print "hello world";
> exit
```

## Examples

### Hello World

```pt
print "hello world";
```

### Variables & Arithmetic

```pt
var x = 10;
var y = 3;
print x + y;     // 13
print x * y;     // 30
print (x - y) / 2;  // 3.5
```

### Conditionals

```pt
if (x > y) {
  print "x is bigger";
} else {
  print "nope";
}
```

### Loops

```pt
// while
var i = 0;
while (i < 5) {
  print i;
  i = i + 1;
}

// for
for (var n = 0; n < 5; n = n + 1) {
  print n;
}

// break / continue
var a = 0;
while (a < 10) {
  if (a == 3) break;
  if (a == 1) { a = a + 1; continue; }
  print a;
  a = a + 1;
}
```

### Functions & Recursion

```pt
fun fact(n) {
  if (n <= 1) return 1;
  return n * fact(n - 1);
}
print fact(6);  // 720

// with for loop
fun fact2(n) {
  var result = 1;
  for (var i = 1; i <= n; i = i + 1) {
    result = result * i;
  }
  return result;
}
print fact2(5);  // 120
```

### Closures

```pt
fun makeCounter() {
  var count = 0;
  fun counter() {
    count = count + 1;
    return count;
  }
  return counter;
}

var c = makeCounter();
print c();  // 1
print c();  // 2
print c();  // 3
```

### Scope

```pt
var x = "outer";
{
  print x;    // outer
  var x = "inner";
  print x;    // inner
}
print x;      // outer
```

### Built-in Functions

```pt
len("hello")       // 5
toNum("42")        // 42
toNum("abc")       // nil
toString(42)       // "42"
input("name: ")    // reads a line from stdin
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
g++ -std=c++17 -o pt src/main.cpp src/lexer.cpp src/parser.cpp src/interpreter.cpp
```

## Grammar

```
program      → declaration* EOF
declaration  → funDecl | varDecl | statement
funDecl      → "fun" IDENTIFIER "(" parameters? ")" block
varDecl      → "var" IDENTIFIER ("=" expression)? ";"
statement    → exprStmt | printStmt | block | ifStmt | whileStmt
             | forStmt | breakStmt | continueStmt | returnStmt
exprStmt     → expression ";"
printStmt    → "print" expression ";"
block        → "{" declaration* "}"
ifStmt       → "if" "(" expression ")" statement ("else" statement)?
whileStmt    → "while" "(" expression ")" statement
forStmt      → "for" "(" (varDecl | exprStmt | ";") expression? ";" expression? ")" statement
breakStmt    → "break" ";"
continueStmt → "continue" ";"
returnStmt   → "return" expression? ";"
expression   → assignment
assignment   → IDENTIFIER "=" assignment | or
or           → and ("or" and)*
and          → equality ("and" equality)*
equality     → comparison (("==" | "!=") comparison)*
comparison   → term (("<" | "<=" | ">" | ">=") term)*
term         → factor (("+" | "-") factor)*
factor       → unary (("*" | "/") unary)*
unary        → ("!" | "-") unary | call
call         → primary ("(" arguments? ")")*
primary      → NUMBER | STRING | "true" | "false" | "nil"
             | IDENTIFIER | "(" expression ")"
```

## License

MIT
