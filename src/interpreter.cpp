#include "interpreter.h"
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <cstdlib>
#include <cmath>

class ReturnException : public std::exception {
public:
  PTValue value;
  ReturnException(PTValue v) : value(v) {}
};

class BreakException : public std::exception {};
class ContinueException : public std::exception {};

void Interpreter::defineVar(const std::string& name, const PTValue& value) {
  env->values[name] = value;
}

void Interpreter::assignVar(const std::string& name, const PTValue& value) {
  auto e = env;
  while (e) {
    if (e->values.count(name)) { e->values[name] = value; return; }
    e = e->enclosing;
  }
  throw std::runtime_error("Undefined variable '" + name + "'");
}

PTValue Interpreter::getVar(const std::string& name) {
  auto e = env;
  while (e) {
    auto f = e->values.find(name);
    if (f != e->values.end()) return f->second;
    e = e->enclosing;
  }
  throw std::runtime_error("Undefined variable '" + name + "'");
}

bool Interpreter::varExists(const std::string& name) {
  auto e = env;
  while (e) {
    if (e->values.count(name)) return true;
    e = e->enclosing;
  }
  return false;
}

void Interpreter::interpret(std::vector<std::unique_ptr<Stmt>>& stmts) {
  globals = std::make_shared<Environment>();
  env = globals;
  try {
    for (auto& stmt : stmts) execute(*stmt);
  } catch (const ReturnException&) {}
}

void Interpreter::execute(Stmt& stmt) {
  if (auto* p = dynamic_cast<PrintStmt*>(&stmt)) {
    PTValue val = evaluate(p->expression.get());
    if (val.isFunction) std::cout << "<fn>" << std::endl;
    else std::cout << val.value << std::endl;
  } else if (auto* e = dynamic_cast<ExprStmt*>(&stmt)) {
    evaluate(e->expression.get());
  } else if (auto* v = dynamic_cast<VarStmt*>(&stmt)) {
    PTValue val;
    if (v->initializer) val = evaluate(v->initializer.get());
    defineVar(v->name, val);
  } else if (auto* b = dynamic_cast<BlockStmt*>(&stmt)) {
    auto blockEnv = std::make_shared<Environment>(env);
    executeBlock(b->statements, blockEnv);
  } else if (auto* i = dynamic_cast<IfStmt*>(&stmt)) {
    auto cond = evaluate(i->condition.get());
    if (isTruthy(cond)) {
      execute(*i->thenBranch);
    } else if (i->elseBranch) {
      execute(*i->elseBranch);
    }
  } else if (auto* w = dynamic_cast<WhileStmt*>(&stmt)) {
    while (isTruthy(evaluate(w->condition.get()))) {
      try {
        execute(*w->body);
      } catch (const BreakException&) {
        break;
      } catch (const ContinueException&) {
        continue;
      }
    }
  } else if (auto* f = dynamic_cast<FunctionStmt*>(&stmt)) {
    auto func = std::make_shared<PTFunction>();
    func->params = f->params;
    func->body = std::move(f->body);
    func->closure = env;
    defineVar(f->name, PTValue(func));
  } else if (auto* r = dynamic_cast<ReturnStmt*>(&stmt)) {
    PTValue val;
    if (r->value) val = evaluate(r->value.get());
    throw ReturnException(val);
  } else if (dynamic_cast<BreakStmt*>(&stmt)) {
    throw BreakException();
  } else if (dynamic_cast<ContinueStmt*>(&stmt)) {
    throw ContinueException();
  } else if (auto* fr = dynamic_cast<ForStmt*>(&stmt)) {
    auto forEnv = std::make_shared<Environment>(env);
    auto prev = env;
    env = forEnv;

    if (fr->initializer) execute(*fr->initializer);

    while (true) {
      if (fr->condition && !isTruthy(evaluate(fr->condition.get()))) break;
      try {
        execute(*fr->body);
      } catch (const BreakException&) {
        break;
      } catch (const ContinueException&) {
        if (fr->increment) evaluate(fr->increment.get());
        continue;
      }
      if (fr->increment) evaluate(fr->increment.get());
    }

    env = prev;
  }
}

void Interpreter::executeBlock(std::vector<std::unique_ptr<Stmt>>& stmts,
                                std::shared_ptr<Environment> blockEnv) {
  auto prev = env;
  env = blockEnv;
  try {
    for (auto& stmt : stmts) execute(*stmt);
  } catch (const ReturnException&) {
    env = prev;
    throw;
  } catch (const BreakException&) {
    env = prev;
    throw;
  } catch (const ContinueException&) {
    env = prev;
    throw;
  }
  env = prev;
}

PTValue Interpreter::evaluate(Expr* expr) {
  if (auto* l = dynamic_cast<Literal*>(expr)) {
    if (l->isNumber) return PTValue(formatNumber(l->value));
    return PTValue(l->value);
  }
  if (auto* v = dynamic_cast<Variable*>(expr)) return getVar(v->name);
  if (auto* g = dynamic_cast<Grouping*>(expr)) return evaluate(g->expression.get());
  if (auto* u = dynamic_cast<Unary*>(expr)) {
    auto right = evaluate(u->right.get());
    if (right.isFunction) throw std::runtime_error("Cannot use unary on function");
    if (u->op == "-") return PTValue(formatNumber(std::to_string(-std::stod(right.value))));
    if (u->op == "!") return PTValue(isTruthy(right) ? "false" : "true");
  }
  if (auto* b = dynamic_cast<Binary*>(expr)) {
    auto left = evaluate(b->left.get());
    auto right = evaluate(b->right.get());
    if (left.isFunction || right.isFunction) throw std::runtime_error("Cannot use binary on function");

    if (b->op == "+") {
      char* e1, *e2;
      double l = std::strtod(left.value.c_str(), &e1);
      double r = std::strtod(right.value.c_str(), &e2);
      if (*e1 == 0 && *e2 == 0) return PTValue(formatNumber(std::to_string(l + r)));
      return PTValue(left.value + right.value);
    }
    if (b->op == "-") return PTValue(formatNumber(std::to_string(std::stod(left.value) - std::stod(right.value))));
    if (b->op == "*") return PTValue(formatNumber(std::to_string(std::stod(left.value) * std::stod(right.value))));
    if (b->op == "/") return PTValue(formatNumber(std::to_string(std::stod(left.value) / std::stod(right.value))));
    if (b->op == "==") return PTValue(isEqual(left, right) ? "true" : "false");
    if (b->op == "!=") return PTValue(isEqual(left, right) ? "false" : "true");
    double l = std::stod(left.value), r = std::stod(right.value);
    if (b->op == "<") return PTValue(l < r ? "true" : "false");
    if (b->op == "<=") return PTValue(l <= r ? "true" : "false");
    if (b->op == ">") return PTValue(l > r ? "true" : "false");
    if (b->op == ">=") return PTValue(l >= r ? "true" : "false");
  }
  if (auto* l = dynamic_cast<Logical*>(expr)) {
    auto left = evaluate(l->left.get());
    if (l->op == "or") {
      if (isTruthy(left)) return left;
    } else {
      if (!isTruthy(left)) return left;
    }
    return evaluate(l->right.get());
  }
  if (auto* a = dynamic_cast<Assign*>(expr)) {
    auto val = evaluate(a->value.get());
    assignVar(a->name, val);
    return val;
  }
  if (auto* c = dynamic_cast<Call*>(expr)) {
    // Check for built-in calls (callee is a Variable expr matching built-in name)
    if (auto* var = dynamic_cast<Variable*>(c->callee.get())) {
      if (!varExists(var->name)) {
        PTValue result = callBuiltin(var->name, c->arguments);
        if (!result.isFunction) return result;
      }
    }

    auto callee = evaluate(c->callee.get());
    if (!callee.isFunction) throw std::runtime_error("Can only call functions");
    auto& fn = callee.function;
    if (c->arguments.size() != fn->params.size())
      throw std::runtime_error("Expected " + std::to_string(fn->params.size()) +
                               " arguments but got " + std::to_string(c->arguments.size()));

    auto callEnv = std::make_shared<Environment>(fn->closure);
    for (size_t i = 0; i < fn->params.size(); i++)
      callEnv->values[fn->params[i]] = evaluate(c->arguments[i].get());

    auto prev = env;
    env = callEnv;
    PTValue result;
    try {
      for (auto& stmt : fn->body) execute(*stmt);
    } catch (const ReturnException& re) {
      result = re.value;
    }
    env = prev;
    return result;
  }
  throw std::runtime_error("Unknown expression");
}

PTValue Interpreter::callBuiltin(const std::string& name, const std::vector<std::unique_ptr<Expr>>& args) {
  if (name == "len") {
    if (args.size() != 1) throw std::runtime_error("len() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isFunction) throw std::runtime_error("len() expects a string");
    return PTValue(formatNumber(std::to_string((long long)arg.value.size())));
  }
  if (name == "toNum") {
    if (args.size() != 1) throw std::runtime_error("toNum() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isFunction) throw std::runtime_error("toNum() expects a string");
    try {
      return PTValue(formatNumber(arg.value));
    } catch (...) {
      return PTValue("nil");
    }
  }
  if (name == "toString") {
    if (args.size() != 1) throw std::runtime_error("toString() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isFunction) return PTValue("<fn>");
    return PTValue(arg.value);
  }
  if (name == "input") {
    if (args.size() > 0) {
      auto prompt = evaluate(args[0].get());
      std::cout << prompt.value;
    }
    std::string line;
    if (!std::getline(std::cin, line)) return PTValue("nil");
    return PTValue(line);
  }
  // not a built-in, return a sentinel so caller falls through to normal call
  return PTValue(std::make_shared<PTFunction>());
}

bool Interpreter::isTruthy(const PTValue& val) {
  if (val.isFunction) return true;
  if (val.value == "false" || val.value == "nil") return false;
  return true;
}

bool Interpreter::isEqual(const PTValue& a, const PTValue& b) {
  if (a.isFunction || b.isFunction) return false;
  return a.value == b.value;
}

std::string Interpreter::formatNumber(const std::string& val) {
  double d = std::stod(val);
  if (d == std::floor(d)) return std::to_string((long long)d);
  std::string s = std::to_string(d);
  s.erase(s.find_last_not_of('0') + 1, std::string::npos);
  if (s.back() == '.') s.pop_back();
  return s;
}
