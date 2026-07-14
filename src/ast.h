#pragma once
#include <memory>
#include <vector>
#include <string>

enum class ExprType {
  Literal, Variable, Binary, Unary, Grouping, Assign, Call, Logical,
  ArrayExpr, IndexExpr, AssignIndex, TernaryExpr, MapExpr, DotExpr,
  DotAssignExpr, InterpolatedExpr, LambdaExpr, ThrowExpr, PostfixExpr,
  ListCompExpr, ThisExpr, SuperExpr, MatchCase, MatchExpr
};

enum class StmtType {
  Print, PrintNL, Expr, Var, Const, Block, If, While, Function, Return,
  Break, Continue, For, ForEach, Import, Try, Repeat, Class, Enum, Export
};

struct Expr {
  ExprType type;
  virtual ~Expr() = default;
  Expr(ExprType t) : type(t) {}
};

struct Literal : Expr {
  std::string value;
  bool isNumber, isString, isBool, isNil;
  Literal(std::string v, bool isNum = false, bool isStr = false, bool isB = false, bool isN = false)
    : Expr(ExprType::Literal), value(v), isNumber(isNum), isString(isStr), isBool(isB), isNil(isN) {}
};

struct Variable : Expr {
  std::string name;
  mutable int id = -1;
  Variable(std::string n) : Expr(ExprType::Variable), name(n) {}
};

struct Binary : Expr {
  std::unique_ptr<Expr> left, right;
  std::string op;
  Binary(std::unique_ptr<Expr> l, std::string o, std::unique_ptr<Expr> r)
    : Expr(ExprType::Binary), left(std::move(l)), right(std::move(r)), op(o) {}
};

struct Unary : Expr {
  std::string op;
  std::unique_ptr<Expr> right;
  Unary(std::string o, std::unique_ptr<Expr> r) : Expr(ExprType::Unary), op(o), right(std::move(r)) {}
};

struct Grouping : Expr {
  std::unique_ptr<Expr> expression;
  Grouping(std::unique_ptr<Expr> e) : Expr(ExprType::Grouping), expression(std::move(e)) {}
};

struct Assign : Expr {
  std::string name;
  mutable int id = -1;
  std::unique_ptr<Expr> value;
  Assign(std::string n, std::unique_ptr<Expr> v) : Expr(ExprType::Assign), name(n), value(std::move(v)) {}
};

struct Call : Expr {
  std::unique_ptr<Expr> callee;
  std::vector<std::unique_ptr<Expr>> arguments;
  Call(std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Expr>> a)
    : Expr(ExprType::Call), callee(std::move(c)), arguments(std::move(a)) {}
};

struct Logical : Expr {
  std::unique_ptr<Expr> left, right;
  std::string op;
  Logical(std::unique_ptr<Expr> l, std::string o, std::unique_ptr<Expr> r)
    : Expr(ExprType::Logical), left(std::move(l)), right(std::move(r)), op(o) {}
};

struct ArrayExpr : Expr {
  std::vector<std::unique_ptr<Expr>> elements;
  ArrayExpr(std::vector<std::unique_ptr<Expr>> e) : Expr(ExprType::ArrayExpr), elements(std::move(e)) {}
};

struct IndexExpr : Expr {
  std::unique_ptr<Expr> callee;
  std::unique_ptr<Expr> index;
  IndexExpr(std::unique_ptr<Expr> c, std::unique_ptr<Expr> i)
    : Expr(ExprType::IndexExpr), callee(std::move(c)), index(std::move(i)) {}
};

struct AssignIndex : Expr {
  std::unique_ptr<Expr> callee;
  std::unique_ptr<Expr> index;
  std::unique_ptr<Expr> value;
  AssignIndex(std::unique_ptr<Expr> c, std::unique_ptr<Expr> i, std::unique_ptr<Expr> v)
    : Expr(ExprType::AssignIndex), callee(std::move(c)), index(std::move(i)), value(std::move(v)) {}
};

struct TernaryExpr : Expr {
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Expr> trueBranch;
  std::unique_ptr<Expr> falseBranch;
  TernaryExpr(std::unique_ptr<Expr> c, std::unique_ptr<Expr> t, std::unique_ptr<Expr> f)
    : Expr(ExprType::TernaryExpr), condition(std::move(c)), trueBranch(std::move(t)), falseBranch(std::move(f)) {}
};

struct MapExpr : Expr {
  std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> entries;
  MapExpr(std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> e)
    : Expr(ExprType::MapExpr), entries(std::move(e)) {}
};

struct DotExpr : Expr {
  std::unique_ptr<Expr> object;
  std::string name;
  DotExpr(std::unique_ptr<Expr> o, std::string n) : Expr(ExprType::DotExpr), object(std::move(o)), name(n) {}
};

struct DotAssignExpr : Expr {
  std::unique_ptr<Expr> object;
  std::string name;
  std::unique_ptr<Expr> value;
  DotAssignExpr(std::unique_ptr<Expr> o, std::string n, std::unique_ptr<Expr> v)
    : Expr(ExprType::DotAssignExpr), object(std::move(o)), name(n), value(std::move(v)) {}
};

struct InterpolatedExpr : Expr {
  std::vector<std::string> strings;
  std::vector<std::unique_ptr<Expr>> exprs;
  InterpolatedExpr(std::vector<std::string> s, std::vector<std::unique_ptr<Expr>> e)
    : Expr(ExprType::InterpolatedExpr), strings(std::move(s)), exprs(std::move(e)) {}
};

struct Stmt {
  StmtType stype;
  virtual ~Stmt() = default;
  Stmt(StmtType t) : stype(t) {}
};

struct PrintStmt : Stmt {
  std::unique_ptr<Expr> expression;
  PrintStmt(std::unique_ptr<Expr> e) : Stmt(StmtType::Print), expression(std::move(e)) {}
};

struct PrintNLStmt : Stmt {
  std::unique_ptr<Expr> expression;
  PrintNLStmt(std::unique_ptr<Expr> e) : Stmt(StmtType::PrintNL), expression(std::move(e)) {}
};

struct ExprStmt : Stmt {
  std::unique_ptr<Expr> expression;
  ExprStmt(std::unique_ptr<Expr> e) : Stmt(StmtType::Expr), expression(std::move(e)) {}
};

struct VarStmt : Stmt {
  std::string name;
  mutable int id = -1;
  std::unique_ptr<Expr> initializer;
  VarStmt(std::string n, std::unique_ptr<Expr> i) : Stmt(StmtType::Var), name(n), initializer(std::move(i)) {}
};

struct ConstStmt : Stmt {
  std::string name;
  mutable int id = -1;
  std::unique_ptr<Expr> initializer;
  ConstStmt(std::string n, std::unique_ptr<Expr> i) : Stmt(StmtType::Const), name(n), initializer(std::move(i)) {}
};

struct BlockStmt : Stmt {
  std::vector<std::unique_ptr<Stmt>> statements;
  BlockStmt(std::vector<std::unique_ptr<Stmt>> s) : Stmt(StmtType::Block), statements(std::move(s)) {}
};

struct IfStmt : Stmt {
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> thenBranch, elseBranch;
  IfStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> t, std::unique_ptr<Stmt> e)
    : Stmt(StmtType::If), condition(std::move(c)), thenBranch(std::move(t)), elseBranch(std::move(e)) {}
};

struct WhileStmt : Stmt {
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> body;
  WhileStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> b)
    : Stmt(StmtType::While), condition(std::move(c)), body(std::move(b)) {}
};

struct FunctionStmt : Stmt {
  std::string name;
  mutable int id = -1;
  std::vector<std::string> params;
  mutable std::vector<int> paramIds;
  std::vector<std::unique_ptr<Stmt>> body;
  FunctionStmt(std::string n, std::vector<std::string> p, std::vector<std::unique_ptr<Stmt>> b)
    : Stmt(StmtType::Function), name(n), params(p), body(std::move(b)) {}
};

struct ReturnStmt : Stmt {
  std::unique_ptr<Expr> value;
  ReturnStmt(std::unique_ptr<Expr> v) : Stmt(StmtType::Return), value(std::move(v)) {}
};

struct BreakStmt : Stmt { BreakStmt() : Stmt(StmtType::Break) {} };
struct ContinueStmt : Stmt { ContinueStmt() : Stmt(StmtType::Continue) {} };

struct ForStmt : Stmt {
  std::unique_ptr<Stmt> initializer;
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Expr> increment;
  std::unique_ptr<Stmt> body;
  ForStmt(std::unique_ptr<Stmt> i, std::unique_ptr<Expr> c, std::unique_ptr<Expr> inc, std::unique_ptr<Stmt> b)
    : Stmt(StmtType::For), initializer(std::move(i)), condition(std::move(c)), increment(std::move(inc)), body(std::move(b)) {}
};

struct ForEachStmt : Stmt {
  std::string variable;
  mutable int id = -1;
  std::unique_ptr<Expr> iterable;
  std::unique_ptr<Stmt> body;
  ForEachStmt(std::string v, std::unique_ptr<Expr> it, std::unique_ptr<Stmt> b)
    : Stmt(StmtType::ForEach), variable(v), iterable(std::move(it)), body(std::move(b)) {}
};

struct LambdaExpr : Expr {
  std::vector<std::string> params;
  mutable std::vector<int> paramIds;
  std::vector<std::unique_ptr<Stmt>> body;
  LambdaExpr(std::vector<std::string> p, std::vector<std::unique_ptr<Stmt>> b)
    : Expr(ExprType::LambdaExpr), params(std::move(p)), body(std::move(b)) {}
};

struct ImportStmt : Stmt {
  std::string path;
  std::string alias;
  ImportStmt(std::string p, std::string a = "") : Stmt(StmtType::Import), path(p), alias(a) {}
};

struct TryStmt : Stmt {
  std::vector<std::unique_ptr<Stmt>> tryBody;
  std::string catchVar;
  mutable int catchId = -1;
  std::vector<std::unique_ptr<Stmt>> catchBody;
  std::vector<std::unique_ptr<Stmt>> finallyBody;
  TryStmt(std::vector<std::unique_ptr<Stmt>> tb, std::string cv,
          std::vector<std::unique_ptr<Stmt>> cb, std::vector<std::unique_ptr<Stmt>> fb)
    : Stmt(StmtType::Try), tryBody(std::move(tb)), catchVar(cv), catchBody(std::move(cb)), finallyBody(std::move(fb)) {}
};

struct ThrowExpr : Expr {
  std::unique_ptr<Expr> value;
  ThrowExpr(std::unique_ptr<Expr> v) : Expr(ExprType::ThrowExpr), value(std::move(v)) {}
};

struct PostfixExpr : Expr {
  std::unique_ptr<Expr> operand;
  std::string op;
  PostfixExpr(std::unique_ptr<Expr> o, std::string op) : Expr(ExprType::PostfixExpr), operand(std::move(o)), op(op) {}
};

struct ListCompExpr : Expr {
  std::unique_ptr<Expr> element;
  std::string variable;
  mutable int id = -1;
  std::unique_ptr<Expr> iterable;
  std::unique_ptr<Expr> condition;
  ListCompExpr(std::unique_ptr<Expr> e, std::string v, std::unique_ptr<Expr> it, std::unique_ptr<Expr> c)
    : Expr(ExprType::ListCompExpr), element(std::move(e)), variable(v), iterable(std::move(it)), condition(std::move(c)) {}
};

struct ThisExpr : Expr { ThisExpr() : Expr(ExprType::ThisExpr) {} };

struct SuperExpr : Expr {
  std::string method;
  SuperExpr(std::string m) : Expr(ExprType::SuperExpr), method(m) {}
};

struct MatchCase : Expr {
  std::vector<std::unique_ptr<Expr>> patterns;
  std::unique_ptr<Expr> body;
  MatchCase(std::vector<std::unique_ptr<Expr>> p, std::unique_ptr<Expr> b)
    : Expr(ExprType::MatchCase), patterns(std::move(p)), body(std::move(b)) {}
};

struct MatchExpr : Expr {
  std::unique_ptr<Expr> value;
  std::vector<std::unique_ptr<MatchCase>> cases;
  MatchExpr(std::unique_ptr<Expr> v, std::vector<std::unique_ptr<MatchCase>> c)
    : Expr(ExprType::MatchExpr), value(std::move(v)), cases(std::move(c)) {}
};

struct RepeatStmt : Stmt {
  int count;
  std::vector<std::unique_ptr<Stmt>> body;
  RepeatStmt(int c, std::vector<std::unique_ptr<Stmt>> b) : Stmt(StmtType::Repeat), count(c), body(std::move(b)) {}
};

struct ClassStmt : Stmt {
  std::string name;
  std::string parent;
  std::vector<std::unique_ptr<FunctionStmt>> methods;
  std::vector<std::unique_ptr<FunctionStmt>> staticMethods;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
  ClassStmt(std::string n, std::string p,
            std::vector<std::unique_ptr<FunctionStmt>> m,
            std::vector<std::unique_ptr<FunctionStmt>> sm,
            std::vector<std::pair<std::string, std::unique_ptr<Expr>>> f)
    : Stmt(StmtType::Class), name(n), parent(p), methods(std::move(m)), staticMethods(std::move(sm)), fields(std::move(f)) {}
};

struct EnumStmt : Stmt {
  std::string name;
  std::vector<std::string> values;
  EnumStmt(std::string n, std::vector<std::string> v) : Stmt(StmtType::Enum), name(n), values(std::move(v)) {}
};

struct ExportStmt : Stmt {
  std::unique_ptr<FunctionStmt> func;
  ExportStmt(std::unique_ptr<FunctionStmt> f) : Stmt(StmtType::Export), func(std::move(f)) {}
};
