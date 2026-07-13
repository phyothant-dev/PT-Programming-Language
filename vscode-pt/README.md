# PT Language Support for VS Code

Syntax highlighting, snippets, and one-click run for the PT programming language.

## Install

### Option 1: Manual install (recommended)

1. Download this folder (`vscode-pt`) from the repo
2. Open VS Code
3. Press `Ctrl+Shift+P` → type "Install from VSIX"
4. Or run:
   ```sh
   code --install-extension ./vscode-pt
   ```

### Option 2: From the folder

1. Copy the `vscode-pt` folder anywhere
2. In VS Code, press `Ctrl+Shift+P`
3. Type "Extensions: Install from Local..."
4. Select the `vscode-pt` folder

## Usage

### Write code

Create a new file with `.pt` extension. You get:
- **Syntax highlighting** — keywords, strings, numbers, functions all colored
- **Auto-closing brackets** — type `{` and get `}`
- **Code snippets** — type shortcuts and press Tab:
  - `show` + Tab → `show("");`
  - `fn` + Tab → function template
  - `if` + Tab → if statement
  - `for` + Tab → for loop
  - `foreach` + Tab → for-each loop
  - `while` + Tab → while loop
  - `loop` + Tab → infinite loop
  - `try` + Tab → try/catch
  - `lambda` + Tab → `() => `

### Run code

**Method 1: Keyboard shortcut**
- Press `Ctrl+Alt+R` (Mac: `Cmd+Alt+R`)

**Method 2: Command palette**
- Press `Ctrl+Shift+P`
- Type "PT: Run File"

**Method 3: Output panel**
- The PT output appears in the "PT" output panel at the bottom

### Snippet examples

| Type this | Press Tab | Get this |
|-----------|-----------|----------|
| `show` | Tab | `show("");` |
| `fn` | Tab | `fn name() { }` |
| `fn.` | Tab | `fn name() => ;` |
| `if` | Tab | `if () { }` |
| `ife` | Tab | `if () { } else { }` |
| `elif` | Tab | `} else if () {` |
| `for` | Tab | `for (let i = 0; ...) { }` |
| `foreach` | Tab | `for (x in arr) { }` |
| `while` | Tab | `while () { }` |
| `loop` | Tab | `loop { }` |
| `try` | Tab | `try { } catch (e) { }` |
| `lambda` | Tab | `() => ` |
| `const` | Tab | `const NAME = ;` |
| `main` | Tab | Hello World template |
