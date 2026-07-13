#pragma once
#include <memory>
#include <vector>
#include <string>

struct Expr {
  virtual ~Expr() = default;
};

struct Literal : Expr {
  std::string value;
  bool isNumber, isString, isBool, isNil;
  Literal(std::string v, bool isNum = false, bool isStr = false, bool isB = false, bool isN = false)
    : value(v), isNumber(isNum), isString(isStr), isBool(isB), isNil(isN) {}
};

struct Variable : Expr {
  std::string name;
  Variable(std::string n) : name(n) {}
};

struct Binary : Expr {
  std::unique_ptr<Expr> left, right;
  std::string op;
  Binary(std::unique_ptr<Expr> l, std::string o, std::unique_ptr<Expr> r)
    : left(std::move(l)), op(o), right(std::move(r)) {}
};

struct Unary : Expr {
  std::string op;
  std::unique_ptr<Expr> right;
  Unary(std::string o, std::unique_ptr<Expr> r) : op(o), right(std::move(r)) {}
};

struct Grouping : Expr {
  std::unique_ptr<Expr> expression;
  Grouping(std::unique_ptr<Expr> e) : expression(std::move(e)) {}
};

struct Assign : Expr {
  std::string name;
  std::unique_ptr<Expr> value;
  Assign(std::string n, std::unique_ptr<Expr> v) : name(n), value(std::move(v)) {}
};

struct Call : Expr {
  std::unique_ptr<Expr> callee;
  std::vector<std::unique_ptr<Expr>> arguments;
  Call(std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Expr>> a) : callee(std::move(c)), arguments(std::move(a)) {}
};

struct Logical : Expr {
  std::unique_ptr<Expr> left, right;
  std::string op;
  Logical(std::unique_ptr<Expr> l, std::string o, std::unique_ptr<Expr> r)
    : left(std::move(l)), op(o), right(std::move(r)) {}
};

struct ArrayExpr : Expr {
  std::vector<std::unique_ptr<Expr>> elements;
  ArrayExpr(std::vector<std::unique_ptr<Expr>> e) : elements(std::move(e)) {}
};

struct IndexExpr : Expr {
  std::unique_ptr<Expr> callee;
  std::unique_ptr<Expr> index;
  IndexExpr(std::unique_ptr<Expr> c, std::unique_ptr<Expr> i) : callee(std::move(c)), index(std::move(i)) {}
};

struct AssignIndex : Expr {
  std::unique_ptr<Expr> callee;
  std::unique_ptr<Expr> index;
  std::unique_ptr<Expr> value;
  AssignIndex(std::unique_ptr<Expr> c, std::unique_ptr<Expr> i, std::unique_ptr<Expr> v)
    : callee(std::move(c)), index(std::move(i)), value(std::move(v)) {}
};

struct TernaryExpr : Expr {
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Expr> trueBranch;
  std::unique_ptr<Expr> falseBranch;
  TernaryExpr(std::unique_ptr<Expr> c, std::unique_ptr<Expr> t, std::unique_ptr<Expr> f)
    : condition(std::move(c)), trueBranch(std::move(t)), falseBranch(std::move(f)) {}
};

struct Stmt {
  virtual ~Stmt() = default;
};

struct PrintStmt : Stmt {
  std::unique_ptr<Expr> expression;
  PrintStmt(std::unique_ptr<Expr> e) : expression(std::move(e)) {}
};

struct ExprStmt : Stmt {
  std::unique_ptr<Expr> expression;
  ExprStmt(std::unique_ptr<Expr> e) : expression(std::move(e)) {}
};

struct VarStmt : Stmt {
  std::string name;
  std::unique_ptr<Expr> initializer;
  VarStmt(std::string n, std::unique_ptr<Expr> i) : name(n), initializer(std::move(i)) {}
};

struct BlockStmt : Stmt {
  std::vector<std::unique_ptr<Stmt>> statements;
  BlockStmt(std::vector<std::unique_ptr<Stmt>> s) : statements(std::move(s)) {}
};

struct IfStmt : Stmt {
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> thenBranch, elseBranch;
  IfStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> t, std::unique_ptr<Stmt> e)
    : condition(std::move(c)), thenBranch(std::move(t)), elseBranch(std::move(e)) {}
};

struct WhileStmt : Stmt {
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> body;
  WhileStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> b)
    : condition(std::move(c)), body(std::move(b)) {}
};

struct FunctionStmt : Stmt {
  std::string name;
  std::vector<std::string> params;
  std::vector<std::unique_ptr<Stmt>> body;
  FunctionStmt(std::string n, std::vector<std::string> p, std::vector<std::unique_ptr<Stmt>> b)
    : name(n), params(p), body(std::move(b)) {}
};

struct ReturnStmt : Stmt {
  std::unique_ptr<Expr> value;
  ReturnStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {}
};

struct BreakStmt : Stmt {};
struct ContinueStmt : Stmt {};

struct ForStmt : Stmt {
  std::unique_ptr<Stmt> initializer;
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Expr> increment;
  std::unique_ptr<Stmt> body;
  ForStmt(std::unique_ptr<Stmt> i, std::unique_ptr<Expr> c, std::unique_ptr<Expr> inc, std::unique_ptr<Stmt> b)
    : initializer(std::move(i)), condition(std::move(c)), increment(std::move(inc)), body(std::move(b)) {}
};

struct ForEachStmt : Stmt {
  std::string variable;
  std::unique_ptr<Expr> iterable;
  std::unique_ptr<Stmt> body;
  ForEachStmt(std::string v, std::unique_ptr<Expr> it, std::unique_ptr<Stmt> b)
    : variable(std::move(v)), iterable(std::move(it)), body(std::move(b)) {}
};
