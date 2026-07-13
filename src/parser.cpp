#include "parser.h"
#include "lexer.h"
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
  if (match({TokenType::UNLESS})) return unlessStmt();
  if (match({TokenType::WHILE})) return whileStmt();
  if (match({TokenType::LOOP})) return loopStmt();
  if (match({TokenType::REPEAT})) return repeatStmt();
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
  if (match({TokenType::PRINT})) return showStmt();
  if (match({TokenType::PRINT_NL})) return printStmt();
  if (match({TokenType::TRY})) return tryStmt();
  if (match({TokenType::IMPORT})) return importStmt();
  if (match({TokenType::EXPORT})) return exportStmt();
  if (match({TokenType::MATCH})) return matchStmt();
  if (match({TokenType::CLASS})) return classStmt();
  if (match({TokenType::ENUM})) return enumStmt();
  if (match({TokenType::LBRACE})) return blockStmt();
  if (match({TokenType::VAR})) return varStmt();
  if (match({TokenType::CONST})) return constStmt();
  return exprStmt();
}

std::unique_ptr<FunctionStmt> Parser::functionStmt() {
  Token name = consume(TokenType::IDENTIFIER, "Expect function name");
  consume(TokenType::LPAREN, "Expect '(' after function name");
  std::vector<std::string> params;
  if (!check(TokenType::RPAREN)) {
    do {
      params.push_back(consume(TokenType::IDENTIFIER, "Expect parameter name").value);
    } while (match({TokenType::COMMA}));
  }
  consume(TokenType::RPAREN, "Expect ')' after parameters");

  if (match({TokenType::ARROW})) {
    auto bodyExpr = expression();
    consume(TokenType::SEMICOLON, "Expect ';' after arrow function");
    auto ret = std::make_unique<ReturnStmt>(std::move(bodyExpr));
    std::vector<std::unique_ptr<Stmt>> body;
    body.push_back(std::move(ret));
    return std::make_unique<FunctionStmt>(name.value, params, std::move(body));
  }

  consume(TokenType::LBRACE, "Expect '{' before function body");
  auto body = block();
  return std::make_unique<FunctionStmt>(name.value, params, std::move(body));
}

std::unique_ptr<Stmt> Parser::funStmt() {
  return functionStmt();
}

std::unique_ptr<Stmt> Parser::returnStmt() {
  std::unique_ptr<Expr> value;
  if (!check(TokenType::SEMICOLON)) value = expression();
  consume(TokenType::SEMICOLON, "Expect ';' after return value");
  return std::make_unique<ReturnStmt>(std::move(value));
}

std::unique_ptr<Stmt> Parser::showStmt() {
  auto expr = expression();
  consume(TokenType::SEMICOLON, "Expect ';' after value");
  return std::make_unique<PrintStmt>(std::move(expr));
}

std::unique_ptr<Stmt> Parser::printStmt() {
  auto expr = expression();
  consume(TokenType::SEMICOLON, "Expect ';' after value");
  return std::make_unique<PrintNLStmt>(std::move(expr));
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

std::unique_ptr<Stmt> Parser::constStmt() {
  Token name = consume(TokenType::IDENTIFIER, "Expect constant name");
  consume(TokenType::EQ, "Expect '=' after constant name");
  auto init = expression();
  consume(TokenType::SEMICOLON, "Expect ';' after constant declaration");
  return std::make_unique<ConstStmt>(name.value, std::move(init));
}

std::unique_ptr<Stmt> Parser::importStmt() {
  auto pathToken = consume(TokenType::STRING, "Expect module path after 'import'");
  std::string alias;
  if (match({TokenType::IDENTIFIER}) && previous().value == "as") {
    alias = consume(TokenType::IDENTIFIER, "Expect alias after 'as'").value;
  }
  consume(TokenType::SEMICOLON, "Expect ';' after import");
  return std::make_unique<ImportStmt>(pathToken.value, alias);
}

std::unique_ptr<Stmt> Parser::exportStmt() {
  auto func = funStmt();
  auto f = dynamic_cast<FunctionStmt*>(func.get());
  if (!f) throw std::runtime_error("Export can only export functions");
  func.release();
  return std::make_unique<ExportStmt>(std::unique_ptr<FunctionStmt>(f));
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
  if (match({TokenType::ELSE})) {
    if (check(TokenType::IF)) {
      advance();
      elseB = ifStmt();
    } else {
      elseB = statement();
    }
  } else if (match({TokenType::ELIF})) {
    elseB = ifStmt();
  }
  return std::make_unique<IfStmt>(std::move(cond), std::move(thenB), std::move(elseB));
}

std::unique_ptr<Stmt> Parser::whileStmt() {
  consume(TokenType::LPAREN, "Expect '(' after 'while'");
  auto cond = expression();
  consume(TokenType::RPAREN, "Expect ')' after condition");
  auto body = statement();
  return std::make_unique<WhileStmt>(std::move(cond), std::move(body));
}

std::unique_ptr<Stmt> Parser::unlessStmt() {
  consume(TokenType::LPAREN, "Expect '(' after 'unless'");
  auto cond = expression();
  consume(TokenType::RPAREN, "Expect ')' after condition");
  auto body = statement();
  auto negCond = std::make_unique<Unary>("!", std::move(cond));
  std::unique_ptr<Stmt> elseB;
  return std::make_unique<IfStmt>(std::move(negCond), std::move(body), std::move(elseB));
}

std::unique_ptr<Stmt> Parser::loopStmt() {
  auto body = statement();
  auto trueLit = std::make_unique<Literal>("true", false, false, true);
  return std::make_unique<WhileStmt>(std::move(trueLit), std::move(body));
}

std::unique_ptr<Stmt> Parser::repeatStmt() {
  auto countToken = consume(TokenType::NUMBER, "Expect count after 'repeat'");
  int count = std::stoi(countToken.value);
  consume(TokenType::LBRACE, "Expect '{' after repeat count");
  auto body = block();
  return std::make_unique<RepeatStmt>(count, std::move(body));
}

std::unique_ptr<Expr> Parser::matchExpr() {
  consume(TokenType::LPAREN, "Expect '(' after 'match'");
  auto value = expression();
  consume(TokenType::RPAREN, "Expect ')' after match value");
  consume(TokenType::LBRACE, "Expect '{' after match");
  std::vector<std::unique_ptr<MatchCase>> cases;
  while (!check(TokenType::RBRACE) && !check(TokenType::EOF_)) {
    std::vector<std::unique_ptr<Expr>> patterns;
    do {
      patterns.push_back(expression());
    } while (match({TokenType::COMMA}));
    consume(TokenType::ARROW, "Expect '=>' after match pattern");
    auto body = expression();
    cases.push_back(std::make_unique<MatchCase>(std::move(patterns), std::move(body)));
  }
  consume(TokenType::RBRACE, "Expect '}' after match cases");
  return std::make_unique<MatchExpr>(std::move(value), std::move(cases));
}

std::unique_ptr<Stmt> Parser::matchStmt() {
  auto expr = matchExpr();
  return std::make_unique<ExprStmt>(std::move(expr));
}

std::unique_ptr<Stmt> Parser::classStmt() {
  Token name = consume(TokenType::IDENTIFIER, "Expect class name");
  std::string parent;
  if (match({TokenType::LT})) {
    parent = consume(TokenType::IDENTIFIER, "Expect parent class name").value;
  }
  consume(TokenType::LBRACE, "Expect '{' after class name");
  std::vector<std::unique_ptr<FunctionStmt>> methods;
  std::vector<std::unique_ptr<FunctionStmt>> staticMethods;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
  while (!check(TokenType::RBRACE) && !check(TokenType::EOF_)) {
    if (match({TokenType::STATIC})) {
      if (match({TokenType::FUN})) {
        staticMethods.push_back(functionStmt());
      } else {
        Token fieldName = consume(TokenType::IDENTIFIER, "Expect field name");
        std::unique_ptr<Expr> init;
        if (match({TokenType::EQ})) init = expression();
        consume(TokenType::SEMICOLON, "Expect ';' after field");
        fields.push_back({fieldName.value, std::move(init)});
      }
    } else if (match({TokenType::FUN})) {
      methods.push_back(functionStmt());
    } else {
      Token fieldName = consume(TokenType::IDENTIFIER, "Expect field name");
      std::unique_ptr<Expr> init;
      if (match({TokenType::EQ})) init = expression();
      consume(TokenType::SEMICOLON, "Expect ';' after field");
      fields.push_back({fieldName.value, std::move(init)});
    }
  }
  consume(TokenType::RBRACE, "Expect '}' after class body");
  return std::make_unique<ClassStmt>(name.value, parent, std::move(methods), std::move(staticMethods), std::move(fields));
}

std::unique_ptr<Stmt> Parser::enumStmt() {
  Token name = consume(TokenType::IDENTIFIER, "Expect enum name");
  consume(TokenType::LBRACE, "Expect '{' after enum name");
  std::vector<std::string> values;
  do {
    values.push_back(consume(TokenType::IDENTIFIER, "Expect enum value").value);
  } while (match({TokenType::COMMA}));
  consume(TokenType::RBRACE, "Expect '}' after enum values");
  return std::make_unique<EnumStmt>(name.value, std::move(values));
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

std::unique_ptr<Stmt> Parser::tryStmt() {
  consume(TokenType::LBRACE, "Expect '{' after 'try'");
  auto tryBody = block();
  std::string catchVar;
  std::vector<std::unique_ptr<Stmt>> catchBody;
  std::vector<std::unique_ptr<Stmt>> finallyBody;
  if (match({TokenType::CATCH})) {
    if (match({TokenType::LPAREN})) {
      catchVar = consume(TokenType::IDENTIFIER, "Expect error variable name").value;
      consume(TokenType::RPAREN, "Expect ')' after catch variable");
    }
    consume(TokenType::LBRACE, "Expect '{' after 'catch'");
    catchBody = block();
  }
  if (match({TokenType::FINALLY})) {
    consume(TokenType::LBRACE, "Expect '{' after 'finally'");
    finallyBody = block();
  }
  return std::make_unique<TryStmt>(std::move(tryBody), catchVar, std::move(catchBody), std::move(finallyBody));
}

// Expression parsing

std::unique_ptr<Expr> Parser::expression() { return assignment(); }

std::unique_ptr<Expr> Parser::assignment() {
  auto expr = ternary();
  if (match({TokenType::EQ})) {
    auto value = assignment();
    if (auto* v = dynamic_cast<Variable*>(expr.get()))
      return std::make_unique<Assign>(v->name, std::move(value));
    if (auto* d = dynamic_cast<DotExpr*>(expr.get()))
      return std::make_unique<DotAssignExpr>(std::move(d->object), d->name, std::move(value));
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
  while (match({TokenType::EQ_EQ, TokenType::BANG_EQ, TokenType::IS, TokenType::ISNT, TokenType::IN})) {
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
  if (match({TokenType::BANG, TokenType::MINUS, TokenType::NOT})) {
    auto op = previous().value;
    auto right = unary();
    return std::make_unique<Unary>(op, std::move(right));
  }
  if (match({TokenType::THROW})) {
    auto val = unary();
    return std::make_unique<ThrowExpr>(std::move(val));
  }
  return call();
}

std::unique_ptr<Expr> Parser::call() {
  auto expr = primary();
  while (true) {
    if (match({TokenType::DOT})) {
      auto name = consume(TokenType::IDENTIFIER, "Expect property name after '.'");
      expr = std::make_unique<DotExpr>(std::move(expr), name.value);
    } else if (match({TokenType::LPAREN})) {
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
    } else if (match({TokenType::PLUS_PLUS})) {
      auto op = previous().value;
      expr = std::make_unique<PostfixExpr>(std::move(expr), op);
    } else if (match({TokenType::MINUS_MINUS})) {
      auto op = previous().value;
      expr = std::make_unique<PostfixExpr>(std::move(expr), op);
    } else {
      break;
    }
  }
  return expr;
}

std::unique_ptr<Expr> Parser::primary() {
  if (match({TokenType::NUMBER}))
    return std::make_unique<Literal>(previous().value, true);
  if (match({TokenType::STRING})) {
    auto raw = previous().value;
    if (raw.find("${") != std::string::npos) {
      std::vector<std::string> strings;
      std::vector<std::unique_ptr<Expr>> exprs;
      size_t pos = 0;
      while (pos < raw.size()) {
        auto dollar = raw.find("${", pos);
        if (dollar == std::string::npos) {
          strings.push_back(raw.substr(pos));
          break;
        }
        strings.push_back(raw.substr(pos, dollar - pos));
        auto close = raw.find("}", dollar + 2);
        if (close == std::string::npos) throw std::runtime_error("Unterminated interpolation at line " + std::to_string(previous().line));
        std::string exprStr = raw.substr(dollar + 2, close - dollar - 2);
        Lexer innerLexer(exprStr);
        auto innerTokens = innerLexer.scan();
        Parser innerParser(innerTokens);
        exprs.push_back(innerParser.expression());
        pos = close + 1;
      }
      return std::make_unique<InterpolatedExpr>(std::move(strings), std::move(exprs));
    }
    return std::make_unique<Literal>(raw, false, true);
  }
  if (match({TokenType::TRUE}))
    return std::make_unique<Literal>("true", false, false, true);
  if (match({TokenType::FALSE}))
    return std::make_unique<Literal>("false", false, false, true);
  if (match({TokenType::NIL}))
    return std::make_unique<Literal>("nil", false, false, false, true);
  if (match({TokenType::IDENTIFIER}))
    return std::make_unique<Variable>(previous().value);
  if (match({TokenType::THIS}))
    return std::make_unique<ThisExpr>();
  if (match({TokenType::MATCH}))
    return matchExpr();
  if (match({TokenType::SUPER})) {
    consume(TokenType::DOT, "Expect '.' after 'super'");
    auto method = consume(TokenType::IDENTIFIER, "Expect method name after 'super.'");
    return std::make_unique<SuperExpr>(method.value);
  }
  if (match({TokenType::LBRACKET})) {
    if (check(TokenType::RBRACKET)) {
      advance();
      return std::make_unique<ArrayExpr>(std::vector<std::unique_ptr<Expr>>());
    }
    auto first = expression();
    if (match({TokenType::FOR})) {
      auto varName = consume(TokenType::IDENTIFIER, "Expect variable name after 'for'").value;
      consume(TokenType::IN, "Expect 'in' after variable name in list comprehension");
      auto iterable = expression();
      std::unique_ptr<Expr> cond;
      if (match({TokenType::IF})) {
        cond = expression();
      }
      consume(TokenType::RBRACKET, "Expect ']' after list comprehension");
      return std::make_unique<ListCompExpr>(std::move(first), std::move(varName), std::move(iterable), std::move(cond));
    }
    std::vector<std::unique_ptr<Expr>> elements;
    elements.push_back(std::move(first));
    while (match({TokenType::COMMA})) {
      elements.push_back(expression());
    }
    consume(TokenType::RBRACKET, "Expect ']' after array elements");
    return std::make_unique<ArrayExpr>(std::move(elements));
  }
  if (match({TokenType::LBRACE})) {
    std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> entries;
    if (!check(TokenType::RBRACE)) {
      do {
        std::unique_ptr<Expr> key;
        if (check(TokenType::IDENTIFIER) && tokens[current + 1].type == TokenType::COLON) {
          Token keyTok = advance();
          key = std::make_unique<Literal>(keyTok.value, false, false, false, false);
        } else {
          key = expression();
        }
        consume(TokenType::COLON, "Expect ':' after map key");
        auto val = expression();
        entries.push_back({std::move(key), std::move(val)});
      } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RBRACE, "Expect '}' after map entries");
    return std::make_unique<MapExpr>(std::move(entries));
  }
  if (match({TokenType::LPAREN})) {
    int saved = current;
    std::vector<std::string> params;
    bool isLambda = false;
    if (check(TokenType::IDENTIFIER)) {
      params.push_back(advance().value);
      while (match({TokenType::COMMA})) {
        params.push_back(consume(TokenType::IDENTIFIER, "Expect parameter name").value);
      }
      if (match({TokenType::RPAREN}) && check(TokenType::ARROW)) {
        isLambda = true;
      }
    }
    if (check(TokenType::RPAREN) && current + 1 < (int)tokens.size() && tokens[current + 1].type == TokenType::ARROW) {
      if (!isLambda) {
        params.clear();
        match({TokenType::RPAREN});
        isLambda = true;
      }
    }
    if (isLambda) {
      advance();
      std::vector<std::unique_ptr<Stmt>> body;
      if (check(TokenType::LBRACE)) {
        advance();
        body = block();
      } else {
        auto val = expression();
        body.push_back(std::make_unique<ReturnStmt>(std::move(val)));
      }
      return std::make_unique<LambdaExpr>(std::move(params), std::move(body));
    }
    current = saved;
    auto expr = expression();
    consume(TokenType::RPAREN, "Expect ')' after expression");
    return std::make_unique<Grouping>(std::move(expr));
  }
  throw std::runtime_error(std::string("Unexpected token '") + peek().value + "' at line " + std::to_string(peek().line));
}
