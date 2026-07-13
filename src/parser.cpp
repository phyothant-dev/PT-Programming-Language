#include "parser.h"
#include <stdexcept>

Token Parser::peek() { return tokens[current]; }
Token Parser::previous() { return tokens[current - 1]; }
Token Parser::advance() { if (!check(TokenType::EOF_)) current++; return previous(); }
bool Parser::check(TokenType type) { return peek().type == type; }

bool Parser::match(std::initializer_list<TokenType> types) {
  for (auto t : types) {
    if (check(t)) { advance(); return true; }
  }
  return false;
}

Token Parser::consume(TokenType type, std::string message) {
  if (check(type)) return advance();
  throw std::runtime_error(message + " at line " + std::to_string(peek().line));
}

std::vector<std::unique_ptr<Stmt>> Parser::parse() {
  std::vector<std::unique_ptr<Stmt>> stmts;
  while (!check(TokenType::EOF_)) stmts.push_back(statement());
  return stmts;
}

std::unique_ptr<Stmt> Parser::statement() {
  if (match({TokenType::FOR})) return forStmt();
  if (match({TokenType::IF})) return ifStmt();
  if (match({TokenType::WHILE})) return whileStmt();
  if (match({TokenType::FUN})) return funStmt();
  if (match({TokenType::RETURN})) return returnStmt();
  if (match({TokenType::BREAK})) {
    consume(TokenType::SEMICOLON, "Expect ';' after 'break'");
    return std::make_unique<BreakStmt>();
  }
  if (match({TokenType::CONTINUE})) {
    consume(TokenType::SEMICOLON, "Expect ';' after 'continue'");
    return std::make_unique<ContinueStmt>();
  }
  if (match({TokenType::PRINT})) return printStmt();
  if (match({TokenType::LBRACE})) return blockStmt();
  if (match({TokenType::VAR})) return varStmt();
  return exprStmt();
}

std::unique_ptr<Stmt> Parser::funStmt() {
  Token name = consume(TokenType::IDENTIFIER, "Expect function name");
  consume(TokenType::LPAREN, "Expect '(' after function name");
  std::vector<std::string> params;
  if (!check(TokenType::RPAREN)) {
    do {
      params.push_back(consume(TokenType::IDENTIFIER, "Expect parameter name").value);
    } while (match({TokenType::COMMA}));
  }
  consume(TokenType::RPAREN, "Expect ')' after parameters");
  consume(TokenType::LBRACE, "Expect '{' before function body");
  auto body = block();
  return std::make_unique<FunctionStmt>(name.value, params, std::move(body));
}

std::unique_ptr<Stmt> Parser::returnStmt() {
  std::unique_ptr<Expr> value;
  if (!check(TokenType::SEMICOLON)) value = expression();
  consume(TokenType::SEMICOLON, "Expect ';' after return value");
  return std::make_unique<ReturnStmt>(std::move(value));
}

std::unique_ptr<Stmt> Parser::printStmt() {
  auto expr = expression();
  consume(TokenType::SEMICOLON, "Expect ';' after value");
  return std::make_unique<PrintStmt>(std::move(expr));
}

std::unique_ptr<Stmt> Parser::exprStmt() {
  auto expr = expression();
  consume(TokenType::SEMICOLON, "Expect ';' after expression");
  return std::make_unique<ExprStmt>(std::move(expr));
}

std::unique_ptr<Stmt> Parser::varStmt() {
  Token name = consume(TokenType::IDENTIFIER, "Expect variable name");
  std::unique_ptr<Expr> init;
  if (match({TokenType::EQ})) init = expression();
  consume(TokenType::SEMICOLON, "Expect ';' after variable declaration");
  return std::make_unique<VarStmt>(name.value, std::move(init));
}

std::unique_ptr<Stmt> Parser::blockStmt() {
  auto stmts = block();
  return std::make_unique<BlockStmt>(std::move(stmts));
}

std::unique_ptr<Stmt> Parser::ifStmt() {
  consume(TokenType::LPAREN, "Expect '(' after 'if'");
  auto cond = expression();
  consume(TokenType::RPAREN, "Expect ')' after condition");
  auto thenB = statement();
  std::unique_ptr<Stmt> elseB;
  if (match({TokenType::ELSE})) elseB = statement();
  return std::make_unique<IfStmt>(std::move(cond), std::move(thenB), std::move(elseB));
}

std::unique_ptr<Stmt> Parser::whileStmt() {
  consume(TokenType::LPAREN, "Expect '(' after 'while'");
  auto cond = expression();
  consume(TokenType::RPAREN, "Expect ')' after condition");
  auto body = statement();
  return std::make_unique<WhileStmt>(std::move(cond), std::move(body));
}

std::unique_ptr<Stmt> Parser::forStmt() {
  consume(TokenType::LPAREN, "Expect '(' after 'for'");

  if (check(TokenType::IDENTIFIER)) {
    Token varName = peek();
    Token next = tokens[current + 1];
    if (next.type == TokenType::IN) {
      advance();
      advance();
      auto iterable = expression();
      consume(TokenType::RPAREN, "Expect ')' after for-each iterable");
      auto body = statement();
      return std::make_unique<ForEachStmt>(varName.value, std::move(iterable), std::move(body));
    }
  }

  std::unique_ptr<Stmt> init;
  if (match({TokenType::VAR})) init = varStmt();
  else if (match({TokenType::SEMICOLON})) init = nullptr;
  else { init = exprStmt(); }

  std::unique_ptr<Expr> cond;
  if (!check(TokenType::SEMICOLON)) cond = expression();
  consume(TokenType::SEMICOLON, "Expect ';' after loop condition");

  std::unique_ptr<Expr> inc;
  if (!check(TokenType::RPAREN)) inc = expression();
  consume(TokenType::RPAREN, "Expect ')' after for clauses");

  auto body = statement();
  return std::make_unique<ForStmt>(std::move(init), std::move(cond), std::move(inc), std::move(body));
}

std::vector<std::unique_ptr<Stmt>> Parser::block() {
  std::vector<std::unique_ptr<Stmt>> stmts;
  while (!check(TokenType::RBRACE) && !check(TokenType::EOF_))
    stmts.push_back(statement());
  consume(TokenType::RBRACE, "Expect '}' after block");
  return stmts;
}

// Expression parsing

std::unique_ptr<Expr> Parser::expression() { return assignment(); }

std::unique_ptr<Expr> Parser::assignment() {
  auto expr = ternary();
  if (match({TokenType::EQ})) {
    auto value = assignment();
    if (auto* v = dynamic_cast<Variable*>(expr.get()))
      return std::make_unique<Assign>(v->name, std::move(value));
    IndexExpr* raw = dynamic_cast<IndexExpr*>(expr.get());
    if (raw) {
      return std::make_unique<AssignIndex>(std::move(raw->callee), std::move(raw->index), std::move(value));
    }
    throw std::runtime_error("Invalid assignment target");
  }
  if (match({TokenType::PLUS_EQ, TokenType::MINUS_EQ, TokenType::STAR_EQ, TokenType::SLASH_EQ, TokenType::PERCENT_EQ})) {
    auto opToken = previous().value;
    auto value = assignment();
    std::string binOp(1, opToken[0]);
    if (auto* v = dynamic_cast<Variable*>(expr.get())) {
      auto varExpr = std::make_unique<Variable>(v->name);
      auto bin = std::make_unique<Binary>(std::move(varExpr), binOp, std::move(value));
      return std::make_unique<Assign>(v->name, std::move(bin));
    }
    throw std::runtime_error("Invalid assignment target");
  }
  return expr;
}

std::unique_ptr<Expr> Parser::ternary() {
  auto expr = or_();
  if (match({TokenType::QUESTION})) {
    auto trueBranch = expression();
    consume(TokenType::COLON, "Expect ':' after true branch of ternary");
    auto falseBranch = assignment();
    return std::make_unique<TernaryExpr>(std::move(expr), std::move(trueBranch), std::move(falseBranch));
  }
  return expr;
}

std::unique_ptr<Expr> Parser::or_() {
  auto expr = and_();
  while (match({TokenType::OR})) {
    auto op = previous().value;
    auto right = and_();
    expr = std::make_unique<Logical>(std::move(expr), op, std::move(right));
  }
  return expr;
}

std::unique_ptr<Expr> Parser::and_() {
  auto expr = equality();
  while (match({TokenType::AND})) {
    auto op = previous().value;
    auto right = equality();
    expr = std::make_unique<Logical>(std::move(expr), op, std::move(right));
  }
  return expr;
}

std::unique_ptr<Expr> Parser::equality() {
  auto expr = comparison();
  while (match({TokenType::EQ_EQ, TokenType::BANG_EQ})) {
    auto op = previous().value;
    auto right = comparison();
    expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
  }
  return expr;
}

std::unique_ptr<Expr> Parser::comparison() {
  auto expr = term();
  while (match({TokenType::LT, TokenType::LT_EQ, TokenType::GT, TokenType::GT_EQ})) {
    auto op = previous().value;
    auto right = term();
    expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
  }
  return expr;
}

std::unique_ptr<Expr> Parser::term() {
  auto expr = factor();
  while (match({TokenType::PLUS, TokenType::MINUS})) {
    auto op = previous().value;
    auto right = factor();
    expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
  }
  return expr;
}

std::unique_ptr<Expr> Parser::factor() {
  auto expr = unary();
  while (match({TokenType::STAR, TokenType::SLASH, TokenType::PERCENT})) {
    auto op = previous().value;
    auto right = unary();
    expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
  }
  return expr;
}

std::unique_ptr<Expr> Parser::unary() {
  if (match({TokenType::BANG, TokenType::MINUS})) {
    auto op = previous().value;
    auto right = unary();
    return std::make_unique<Unary>(op, std::move(right));
  }
  return call();
}

std::unique_ptr<Expr> Parser::call() {
  auto expr = primary();
  while (true) {
    if (match({TokenType::LPAREN})) {
      std::vector<std::unique_ptr<Expr>> args;
      if (!check(TokenType::RPAREN)) {
        do { args.push_back(expression()); } while (match({TokenType::COMMA}));
      }
      consume(TokenType::RPAREN, "Expect ')' after arguments");
      expr = std::make_unique<Call>(std::move(expr), std::move(args));
    } else if (match({TokenType::LBRACKET})) {
      auto index = expression();
      consume(TokenType::RBRACKET, "Expect ']' after index");
      expr = std::make_unique<IndexExpr>(std::move(expr), std::move(index));
    } else {
      break;
    }
  }
  return expr;
}

std::unique_ptr<Expr> Parser::primary() {
  if (match({TokenType::NUMBER}))
    return std::make_unique<Literal>(previous().value, true);
  if (match({TokenType::STRING}))
    return std::make_unique<Literal>(previous().value, false, true);
  if (match({TokenType::TRUE}))
    return std::make_unique<Literal>("true", false, false, true);
  if (match({TokenType::FALSE}))
    return std::make_unique<Literal>("false", false, false, true);
  if (match({TokenType::NIL}))
    return std::make_unique<Literal>("nil", false, false, false, true);
  if (match({TokenType::IDENTIFIER}))
    return std::make_unique<Variable>(previous().value);
  if (match({TokenType::LBRACKET})) {
    std::vector<std::unique_ptr<Expr>> elements;
    if (!check(TokenType::RBRACKET)) {
      do { elements.push_back(expression()); } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RBRACKET, "Expect ']' after array elements");
    return std::make_unique<ArrayExpr>(std::move(elements));
  }
  if (match({TokenType::LPAREN})) {
    auto expr = expression();
    consume(TokenType::RPAREN, "Expect ')' after expression");
    return std::make_unique<Grouping>(std::move(expr));
  }
  throw std::runtime_error(std::string("Unexpected token '") + peek().value + "' at line " + std::to_string(peek().line));
}
