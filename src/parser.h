#pragma once
#include <vector>
#include <memory>
#include "token.h"
#include "ast.h"

class Parser {
public:
  Parser(const std::vector<Token>& tokens) : tokens(tokens) {}
  std::vector<std::unique_ptr<Stmt>> parse();

private:
  std::vector<Token> tokens;
  int current = 0;

  Token peek();
  Token previous();
  Token advance();
  bool check(TokenType type);
  bool match(std::initializer_list<TokenType> types);
  Token consume(TokenType type, std::string message);

  std::unique_ptr<Expr> expression();
  std::unique_ptr<Expr> assignment();
  std::unique_ptr<Expr> ternary();
  std::unique_ptr<Expr> or_();
  std::unique_ptr<Expr> and_();
  std::unique_ptr<Expr> equality();
  std::unique_ptr<Expr> comparison();
  std::unique_ptr<Expr> term();
  std::unique_ptr<Expr> factor();
  std::unique_ptr<Expr> unary();
  std::unique_ptr<Expr> call();
  std::unique_ptr<Expr> primary();

  std::unique_ptr<Stmt> statement();
  std::unique_ptr<Stmt> printStmt();
  std::unique_ptr<Stmt> exprStmt();
  std::unique_ptr<Stmt> varStmt();
  std::unique_ptr<Stmt> blockStmt();
  std::unique_ptr<Stmt> ifStmt();
  std::unique_ptr<Stmt> unlessStmt();
  std::unique_ptr<Stmt> forStmt();
  std::unique_ptr<Stmt> whileStmt();
  std::unique_ptr<Stmt> loopStmt();
  std::unique_ptr<Stmt> funStmt();
  std::unique_ptr<Stmt> returnStmt();
  std::unique_ptr<Stmt> forEachStmt();
  std::vector<std::unique_ptr<Stmt>> block();
};
