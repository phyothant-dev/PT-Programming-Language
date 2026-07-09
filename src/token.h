#pragma once
#include <string>

enum class TokenType {
  LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
  SEMICOLON, COMMA, DOT,
  PLUS, MINUS, STAR, SLASH,
  EQ, EQ_EQ, BANG, BANG_EQ,
  LT, LT_EQ, GT, GT_EQ,
  IDENTIFIER, STRING, NUMBER,
  VAR, PRINT, IF, ELSE, WHILE, FOR, FUN, RETURN,
  BREAK, CONTINUE,
  TRUE, FALSE, NIL,
  AND, OR,
  EOF_
};

struct Token {
  TokenType type;
  std::string value;
  int line;
};
