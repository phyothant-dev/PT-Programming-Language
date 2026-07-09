#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer.h"
#include "parser.h"
#include "interpreter.h"

void run(const std::string& source) {
  try {
    Lexer lexer(source);
    auto tokens = lexer.scan();

    Parser parser(tokens);
    auto stmts = parser.parse();

    Interpreter interpreter;
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
  run(buffer.str());
}

void repl() {
  std::cout << "PT v0.1 - Type your code (exit to quit)" << std::endl;
  while (true) {
    std::cout << "> ";
    std::string line;
    if (!std::getline(std::cin, line)) break;
    if (line == "exit") break;
    run(line);
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
