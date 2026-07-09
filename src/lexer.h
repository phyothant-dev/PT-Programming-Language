#pragma once
#include <vector>
#include "token.h"

class Lexer {
public:
  Lexer(const std::string& source) : source(source) {}
  std::vector<Token> scan();

private:
  std::string source;
  int start = 0, current = 0, line = 1, pos = 0;
  std::vector<Token> tokens;

  bool isAtEnd();
  char advance();
  bool match(char expected);
  char peek();
  char peekNext();
  void addToken(TokenType type);
  void addToken(TokenType type, std::string value);
  void scanToken();
  void string_();
  void number();
  void identifier();
  bool isDigit(char c);
  bool isAlpha(char c);
  bool isAlphaNumeric(char c);
};
