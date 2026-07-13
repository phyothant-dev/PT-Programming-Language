#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer.h"
#include "parser.h"
#include "interpreter.h"

void run(Interpreter& interpreter, const std::string& source) {
  try {
    Lexer lexer(source);
    auto tokens = lexer.scan();

    Parser parser(tokens);
    auto stmts = parser.parse();

    interpreter.interpret(stmts);
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
  }
}

void runFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cerr << "Could not open file: " << path << std::endl;
    return;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  Interpreter interpreter;
  run(interpreter, buffer.str());
}

void repl() {
  std::cout << "PT v0.1 - Type your code (exit to quit)" << std::endl;
  Interpreter interpreter;
  interpreter.replMode = true;
  std::vector<std::unique_ptr<Stmt>> empty;
  interpreter.interpret(empty);

  while (true) {
    std::cout << ">> ";
    std::string line;
    if (!std::getline(std::cin, line)) break;
    if (line == "exit") break;

    if (line.empty()) continue;

    std::string source = line;
    int openBraces = 0, openParens = 0;
    for (char c : source) {
      if (c == '{') openBraces++;
      else if (c == '}') openBraces--;
      else if (c == '(') openParens++;
      else if (c == ')') openParens--;
    }

    if (openBraces > 0 || openParens > 0) {
      while (openBraces > 0 || openParens > 0) {
        std::cout << ".. ";
        std::string cont;
        if (!std::getline(std::cin, cont)) break;
        source += "\n" + cont;
        for (char c : cont) {
          if (c == '{') openBraces++;
          else if (c == '}') openBraces--;
          else if (c == '(') openParens++;
          else if (c == ')') openParens--;
        }
      }
    }

    try {
      Lexer lexer(source);
      auto tokens = lexer.scan();
      Parser parser(tokens);
      auto stmts = parser.parse();
      interpreter.interpret(stmts);
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
    }
  }
}

int main(int argc, char* argv[]) {
  if (argc > 1) {
    runFile(argv[1]);
  } else {
    repl();
  }
  return 0;
}
