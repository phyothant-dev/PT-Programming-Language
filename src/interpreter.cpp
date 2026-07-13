#include "interpreter.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <cctype>

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
  if (!globals) {
    globals = std::make_shared<Environment>();
    env = globals;
  }
  try {
    for (auto& stmt : stmts) execute(*stmt);
  } catch (const ReturnException&) {}
}

std::string Interpreter::formatValue(const PTValue& val) {
  if (val.isFunction) return "<fn>";
  if (val.isArray) {
    std::string s = "[";
    for (size_t i = 0; i < val.array->size(); i++) {
      if (i > 0) s += ", ";
      s += formatValue((*val.array)[i]);
    }
    return s + "]";
  }
  return val.value;
}

void Interpreter::execute(Stmt& stmt) {
  if (auto* p = dynamic_cast<PrintStmt*>(&stmt)) {
    PTValue val = evaluate(p->expression.get());
    std::cout << formatValue(val) << std::endl;
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
  } else if (auto* fe = dynamic_cast<ForEachStmt*>(&stmt)) {
    auto iterable = evaluate(fe->iterable.get());
    if (iterable.isFunction) throw std::runtime_error("for-each requires an array or string");
    auto forEachEnv = std::make_shared<Environment>(env);
    auto prev = env;
    env = forEachEnv;
    if (iterable.isArray) {
      for (auto& elem : *iterable.array) {
        env->values[fe->variable] = elem;
        try {
          execute(*fe->body);
        } catch (const BreakException&) { break; }
        catch (const ContinueException&) { continue; }
      }
    } else {
      for (char c : iterable.value) {
        env->values[fe->variable] = PTValue(std::string(1, c));
        try {
          execute(*fe->body);
        } catch (const BreakException&) { break; }
        catch (const ContinueException&) { continue; }
      }
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
  if (auto* t = dynamic_cast<TernaryExpr*>(expr)) {
    auto cond = evaluate(t->condition.get());
    if (isTruthy(cond)) return evaluate(t->trueBranch.get());
    return evaluate(t->falseBranch.get());
  }
  if (auto* u = dynamic_cast<Unary*>(expr)) {
    auto right = evaluate(u->right.get());
    if (right.isFunction || right.isArray) throw std::runtime_error("Cannot use unary on function or array");
    if (u->op == "-") return PTValue(formatNumber(std::to_string(-std::stod(right.value))));
    if (u->op == "!") return PTValue(isTruthy(right) ? "false" : "true");
  }
  if (auto* b = dynamic_cast<Binary*>(expr)) {
    auto left = evaluate(b->left.get());
    auto right = evaluate(b->right.get());
    if (left.isFunction || right.isFunction) throw std::runtime_error("Cannot use binary on function");

    if (b->op == "+") {
      if (left.isArray || right.isArray) throw std::runtime_error("Cannot add arrays");
      char* e1, *e2;
      double l = std::strtod(left.value.c_str(), &e1);
      double r = std::strtod(right.value.c_str(), &e2);
      if (*e1 == 0 && *e2 == 0) return PTValue(formatNumber(std::to_string(l + r)));
      return PTValue(left.value + right.value);
    }
    if (left.isArray || right.isArray) throw std::runtime_error("Cannot use arithmetic on arrays");
    if (b->op == "-") return PTValue(formatNumber(std::to_string(std::stod(left.value) - std::stod(right.value))));
    if (b->op == "*") return PTValue(formatNumber(std::to_string(std::stod(left.value) * std::stod(right.value))));
    if (b->op == "/") return PTValue(formatNumber(std::to_string(std::stod(left.value) / std::stod(right.value))));
    if (b->op == "%") return PTValue(formatNumber(std::to_string(std::fmod(std::stod(left.value), std::stod(right.value)))));
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
  if (auto* ar = dynamic_cast<ArrayExpr*>(expr)) {
    auto arr = std::make_shared<std::vector<PTValue>>();
    for (auto& e : ar->elements) arr->push_back(evaluate(e.get()));
    return PTValue(arr);
  }
  if (auto* ix = dynamic_cast<IndexExpr*>(expr)) {
    auto callee = evaluate(ix->callee.get());
    auto idx = evaluate(ix->index.get());
    if (idx.isArray || idx.isFunction) throw std::runtime_error("Index must be a number");
    int i = (int)std::stod(idx.value);
    if (callee.isArray) {
      int size = (int)callee.array->size();
      if (i < 0) i += size;
      if (i < 0 || i >= size) throw std::runtime_error("Index out of bounds");
      return (*callee.array)[i];
    }
    if (!callee.isFunction && !callee.isArray) {
      int size = (int)callee.value.size();
      if (i < 0) i += size;
      if (i < 0 || i >= size) throw std::runtime_error("String index out of bounds");
      return PTValue(std::string(1, callee.value[i]));
    }
    throw std::runtime_error("Cannot index into this value");
  }
  if (auto* ai = dynamic_cast<AssignIndex*>(expr)) {
    auto callee = evaluate(ai->callee.get());
    if (!callee.isArray) throw std::runtime_error("Can only assign index into arrays");
    auto idx = evaluate(ai->index.get());
    if (idx.isArray || idx.isFunction) throw std::runtime_error("Array index must be a number");
    int i = (int)std::stod(idx.value);
    int size = (int)callee.array->size();
    if (i < 0) i += size;
    if (i < 0 || i >= size) throw std::runtime_error("Index out of bounds");
    auto val = evaluate(ai->value.get());
    (*callee.array)[i] = val;
    return val;
  }
  if (auto* c = dynamic_cast<Call*>(expr)) {
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
    if (arg.isFunction) throw std::runtime_error("len() expects a string or array");
    if (arg.isArray) return PTValue(formatNumber(std::to_string((long long)arg.array->size())));
    return PTValue(formatNumber(std::to_string((long long)arg.value.size())));
  }
  if (name == "push") {
    if (args.size() != 2) throw std::runtime_error("push() expects 2 arguments");
    auto arr = evaluate(args[0].get());
    if (!arr.isArray) throw std::runtime_error("push() expects an array");
    auto val = evaluate(args[1].get());
    arr.array->push_back(val);
    return PTValue("true");
  }
  if (name == "pop") {
    if (args.size() != 1) throw std::runtime_error("pop() expects 1 argument");
    auto arr = evaluate(args[0].get());
    if (!arr.isArray) throw std::runtime_error("pop() expects an array");
    if (arr.array->empty()) return PTValue("nil");
    auto val = arr.array->back();
    arr.array->pop_back();
    return val;
  }
  if (name == "toNum") {
    if (args.size() != 1) throw std::runtime_error("toNum() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isFunction || arg.isArray) throw std::runtime_error("toNum() expects a string");
    try {
      return PTValue(formatNumber(arg.value));
    } catch (...) {
      return PTValue("nil");
    }
  }
  if (name == "toString") {
    if (args.size() != 1) throw std::runtime_error("toString() expects 1 argument");
    auto arg = evaluate(args[0].get());
    return PTValue(formatValue(arg));
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
  if (name == "readFile") {
    if (args.size() != 1) throw std::runtime_error("readFile() expects 1 argument");
    auto path = evaluate(args[0].get());
    std::ifstream file(path.value);
    if (!file.is_open()) return PTValue("nil");
    std::stringstream buf;
    buf << file.rdbuf();
    return PTValue(buf.str());
  }
  if (name == "writeFile") {
    if (args.size() != 2) throw std::runtime_error("writeFile() expects 2 arguments");
    auto path = evaluate(args[0].get());
    auto content = evaluate(args[1].get());
    std::ofstream file(path.value);
    if (!file.is_open()) return PTValue("false");
    file << content.value;
    return PTValue("true");
  }
  if (name == "abs") {
    if (args.size() != 1) throw std::runtime_error("abs() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw std::runtime_error("abs() expects a number");
    return PTValue(formatNumber(std::to_string(std::fabs(std::stod(arg.value)))));
  }
  if (name == "sqrt") {
    if (args.size() != 1) throw std::runtime_error("sqrt() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw std::runtime_error("sqrt() expects a number");
    return PTValue(formatNumber(std::to_string(std::sqrt(std::stod(arg.value)))));
  }
  if (name == "min") {
    if (args.size() != 2) throw std::runtime_error("min() expects 2 arguments");
    auto a = evaluate(args[0].get());
    auto b = evaluate(args[1].get());
    if (a.isArray || a.isFunction || b.isArray || b.isFunction) throw std::runtime_error("min() expects numbers");
    double da = std::stod(a.value), db = std::stod(b.value);
    return PTValue(formatNumber(std::to_string(da < db ? da : db)));
  }
  if (name == "max") {
    if (args.size() != 2) throw std::runtime_error("max() expects 2 arguments");
    auto a = evaluate(args[0].get());
    auto b = evaluate(args[1].get());
    if (a.isArray || a.isFunction || b.isArray || b.isFunction) throw std::runtime_error("max() expects numbers");
    double da = std::stod(a.value), db = std::stod(b.value);
    return PTValue(formatNumber(std::to_string(da > db ? da : db)));
  }
  if (name == "floor") {
    if (args.size() != 1) throw std::runtime_error("floor() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw std::runtime_error("floor() expects a number");
    return PTValue(formatNumber(std::to_string((long long)std::floor(std::stod(arg.value)))));
  }
  if (name == "ceil") {
    if (args.size() != 1) throw std::runtime_error("ceil() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw std::runtime_error("ceil() expects a number");
    return PTValue(formatNumber(std::to_string((long long)std::ceil(std::stod(arg.value)))));
  }
  if (name == "round") {
    if (args.size() != 1) throw std::runtime_error("round() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw std::runtime_error("round() expects a number");
    return PTValue(formatNumber(std::to_string((long long)std::round(std::stod(arg.value)))));
  }
  if (name == "type") {
    if (args.size() != 1) throw std::runtime_error("type() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isFunction) return PTValue("function");
    if (arg.isArray) return PTValue("array");
    if (arg.value == "nil") return PTValue("nil");
    bool isNum = false;
    try { std::stod(arg.value); isNum = true; } catch (...) {}
    if (isNum) return PTValue("number");
    return PTValue("string");
  }
  if (name == "upper") {
    if (args.size() != 1) throw std::runtime_error("upper() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw std::runtime_error("upper() expects a string");
    std::string s = arg.value;
    for (auto& c : s) c = std::toupper(c);
    return PTValue(s);
  }
  if (name == "lower") {
    if (args.size() != 1) throw std::runtime_error("lower() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw std::runtime_error("lower() expects a string");
    std::string s = arg.value;
    for (auto& c : s) c = std::tolower(c);
    return PTValue(s);
  }
  if (name == "trim") {
    if (args.size() != 1) throw std::runtime_error("trim() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw std::runtime_error("trim() expects a string");
    std::string s = arg.value;
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return PTValue("");
    size_t end = s.find_last_not_of(" \t\n\r");
    return PTValue(s.substr(start, end - start + 1));
  }
  if (name == "substr") {
    if (args.size() < 2 || args.size() > 3) throw std::runtime_error("substr() expects 2 or 3 arguments");
    auto str = evaluate(args[0].get());
    if (str.isArray || str.isFunction) throw std::runtime_error("substr() expects a string");
    auto startArg = evaluate(args[1].get());
    int start = (int)std::stod(startArg.value);
    int len = (int)str.value.size() - start;
    if (args.size() == 3) {
      auto lenArg = evaluate(args[2].get());
      len = (int)std::stod(lenArg.value);
    }
    if (start < 0 || start >= (int)str.value.size()) throw std::runtime_error("substr() start out of bounds");
    if (start + len > (int)str.value.size()) len = (int)str.value.size() - start;
    return PTValue(str.value.substr(start, len));
  }
  if (name == "contains") {
    if (args.size() != 2) throw std::runtime_error("contains() expects 2 arguments");
    auto str = evaluate(args[0].get());
    auto sub = evaluate(args[1].get());
    if (str.isArray || str.isFunction) throw std::runtime_error("contains() expects a string");
    if (sub.isArray || sub.isFunction) throw std::runtime_error("contains() expects a string");
    return PTValue(str.value.find(sub.value) != std::string::npos ? "true" : "false");
  }
  if (name == "replace") {
    if (args.size() != 3) throw std::runtime_error("replace() expects 3 arguments");
    auto str = evaluate(args[0].get());
    auto old = evaluate(args[1].get());
    auto newStr = evaluate(args[2].get());
    if (str.isArray || str.isFunction) throw std::runtime_error("replace() expects a string");
    if (old.isArray || old.isFunction) throw std::runtime_error("replace() expects a string");
    if (newStr.isArray || newStr.isFunction) throw std::runtime_error("replace() expects a string");
    std::string result = str.value;
    size_t pos = 0;
    while ((pos = result.find(old.value, pos)) != std::string::npos) {
      result.replace(pos, old.value.size(), newStr.value);
      pos += newStr.value.size();
    }
    return PTValue(result);
  }
  if (name == "split") {
    if (args.size() != 2) throw std::runtime_error("split() expects 2 arguments");
    auto str = evaluate(args[0].get());
    auto delim = evaluate(args[1].get());
    if (str.isArray || str.isFunction) throw std::runtime_error("split() expects a string");
    if (delim.isArray || delim.isFunction) throw std::runtime_error("split() expects a string");
    auto arr = std::make_shared<std::vector<PTValue>>();
    std::string s = str.value;
    std::string d = delim.value;
    if (d.empty()) {
      for (char c : s) arr->push_back(std::string(1, c));
    } else {
      size_t pos = 0;
      while ((pos = s.find(d)) != std::string::npos) {
        arr->push_back(s.substr(0, pos));
        s.erase(0, pos + d.size());
      }
      arr->push_back(s);
    }
    return PTValue(arr);
  }
  if (name == "assert") {
    if (args.size() < 1 || args.size() > 2) throw std::runtime_error("assert() expects 1 or 2 arguments");
    auto cond = evaluate(args[0].get());
    if (!isTruthy(cond)) {
      std::string msg = "Assertion failed";
      if (args.size() == 2) {
        auto m = evaluate(args[1].get());
        msg = m.value;
      }
      throw std::runtime_error(msg);
    }
    return PTValue("true");
  }
  return PTValue(std::make_shared<PTFunction>());
}

bool Interpreter::isTruthy(const PTValue& val) {
  if (val.isFunction) return true;
  if (val.isArray) return val.array->size() > 0;
  if (val.value == "false" || val.value == "nil") return false;
  return true;
}

bool Interpreter::isEqual(const PTValue& a, const PTValue& b) {
  if (a.isFunction || b.isFunction) return false;
  if (a.isArray || b.isArray) {
    if (!a.isArray || !b.isArray) return false;
    if (a.array->size() != b.array->size()) return false;
    for (size_t i = 0; i < a.array->size(); i++)
      if (!isEqual((*a.array)[i], (*b.array)[i])) return false;
    return true;
  }
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
