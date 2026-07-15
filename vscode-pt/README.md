# PT Language Support for VS Code

Syntax highlighting, snippets, and one-click run for the PT programming language.

## Features

- **Syntax highlighting** — all PT keywords, strings, types, operators
- **20+ code snippets** — type a prefix and press Tab
- **Run command** — execute `.pt` files with `Ctrl+Alt+R`
- **SQLite builtins** — `sqliteOpen`, `sqliteExec`, `sqliteQuery`, `sqliteClose`
- **HTTP builtins** — `httpListen`, `httpGet`, `httpPost`, `httpPut`, `httpDelete`
- **String interpolation** — `${expr}` highlighted inside strings

## Install

### Option 1: Command line

```sh
code --install-extension ./vscode-pt
```

### Option 2: VS Code

1. Press `Ctrl+Shift+P`
2. Type "Extensions: Install from Local..."
3. Select the `vscode-pt` folder

## Usage

### Write code

Create a new file with `.pt` extension. You get syntax highlighting, auto-closing brackets, and snippets.

### Run code

- **Keyboard:** `Ctrl+Alt+R` (Mac: `Cmd+Alt+R`)
- **Command palette:** `Ctrl+Shift+P` → "PT: Run File"

Output appears in the "PT" output panel.

### Snippets

| Prefix | Expands to |
|--------|-----------|
| `show` | `show("");` |
| `let` | `let x = ;` |
| `const` | `const NAME = ;` |
| `fn` | `fn name() { }` |
| `fn.` | `fn name() => ;` |
| `if` | `if () { }` |
| `ife` | `if () { } else { }` |
| `elif` | `} else if () {` |
| `unless` | `unless () { }` |
| `for` | `for (let i = 0; ...) { }` |
| `foreach` | `for (x in arr) { }` |
| `while` | `while () { }` |
| `loop` | `loop { }` |
| `repeat` | `repeat N { }` |
| `match` | `let x = match(val) { }` |
| `try` | `try { } catch (e) { }` |
| `tryf` | `try { } catch (e) { } finally { }` |
| `lambda` | `() => ` |
| `class` | `class Name { init() { } }` |
| `enum` | `enum Name { A, B }` |
| `import` | `import "file" as name;` |
| `http` | `httpListen(port, (req) => { });` |
| `sqlite` | `sqliteOpen("db"); ... sqliteClose(db);` |
| `main` | Hello World template |

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| `pt.executablePath` | `"pt"` | Path to the PT executable |

## Requirements

- PT Programming Language installed and in your PATH
  ```sh
  curl -sSL https://raw.githubusercontent.com/phyothant-dev/PT-Programming-Language/main/install.sh | sh
  ```
