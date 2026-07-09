#pragma once
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include "ast.h"

struct PTFunction;

struct PTValue {
  std::string value;
  std::shared_ptr<PTFunction> function;
  bool isFunction;
  PTValue() : value("nil"), isFunction(false) {}
  PTValue(std::string v) : value(v), isFunction(false) {}
  PTValue(std::shared_ptr<PTFunction> f) : value("<fn>"), function(f), isFunction(true) {}
};

struct Environment {
  std::unordered_map<std::string, PTValue> values;
  std::shared_ptr<Environment> enclosing;
  Environment() = default;
  Environment(std::shared_ptr<Environment> enc) : enclosing(enc) {}
};

struct PTFunction {
  std::vector<std::string> params;
  std::vector<std::unique_ptr<Stmt>> body;
  std::shared_ptr<Environment> closure;
};

class Interpreter {
public:
  void interpret(std::vector<std::unique_ptr<Stmt>>& stmts);

private:
  std::shared_ptr<Environment> globals;
  std::shared_ptr<Environment> env;

  void defineVar(const std::string& name, const PTValue& value);
  void assignVar(const std::string& name, const PTValue& value);
  PTValue getVar(const std::string& name);
  bool varExists(const std::string& name);

  void execute(Stmt& stmt);
  void executeBlock(std::vector<std::unique_ptr<Stmt>>& stmts, std::shared_ptr<Environment> blockEnv);

  PTValue evaluate(Expr* expr);
  PTValue callBuiltin(const std::string& name, const std::vector<std::unique_ptr<Expr>>& args);
  bool isTruthy(const PTValue& val);
  bool isEqual(const PTValue& a, const PTValue& b);
  std::string formatNumber(const std::string& val);
};
