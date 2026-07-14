#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "http.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <random>
#include <chrono>
#include <algorithm>

class ReturnException : public std::exception {
public:
  PTValue value;
  ReturnException(PTValue v) : value(std::move(v)) {}
};

class BreakException : public std::exception {};
class ContinueException : public std::exception {};

void Interpreter::defineVar(const std::string& name, const PTValue& value) {
  env->values[name] = value;
}

void Interpreter::assignVar(const std::string& name, const PTValue& value) {
  auto e = env;
  while (e) {
    auto f = e->values.find(name);
    if (f != e->values.end()) {
      if (e->consts.count(name)) throw PTRuntimeError("Cannot reassign constant '" + name + "'");
      f->second = value;
      return;
    }
    e = e->enclosing;
  }
  throw PTRuntimeError("Undefined variable '" + name + "'");
}

PTValue Interpreter::getVar(const std::string& name) {
  auto e = env;
  while (e) {
    auto f = e->values.find(name);
    if (f != e->values.end()) return f->second;
    e = e->enclosing;
  }
  throw PTRuntimeError("Undefined variable '" + name + "'");
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
  if (val.isFunction) {
    if (val.function && !val.function->name.empty()) return "<fn " + val.function->name + ">";
    return "<fn>";
  }
  if (val.isClass) return "<class " + val.klass->name + ">";
  if (val.isInstance) return "<instance of " + val.instance->klass->name + ">";
  if (val.isArray) {
    std::string s = "[";
    for (size_t i = 0; i < val.array->size(); i++) {
      if (i > 0) s += ", ";
      s += formatValue((*val.array)[i]);
    }
    return s + "]";
  }
  if (val.isMap) {
    std::string s = "{";
    bool first = true;
    for (auto& [k, v] : *val.map) {
      if (!first) s += ", ";
      s += k + ": " + formatValue(v);
      first = false;
    }
    return s + "}";
  }
  return val.value;
}

static inline bool isNumStr(const std::string& s) {
  if (s.empty()) return false;
  const char* p = s.c_str();
  if (*p == '-' || *p == '+') p++;
  if (!*p) return false;
  bool hasDigit = false;
  while (*p >= '0' && *p <= '9') { hasDigit = true; p++; }
  if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { hasDigit = true; p++; } }
  return hasDigit && *p == '\0';
}

// Fast double-from-PTValue that avoids stod when possible
static inline double toDouble(const PTValue& v) {
  if (v.isNumber) return v.numValue;
  return std::stod(v.value);
}

void Interpreter::execute(Stmt& stmt) {
  switch (stmt.stype) {
  case StmtType::Print: {
    auto& p = static_cast<PrintStmt&>(stmt);
    PTValue val = evaluate(p.expression.get());
    std::cout << formatValue(val) << std::endl;
    break;
  }
  case StmtType::PrintNL: {
    auto& p = static_cast<PrintNLStmt&>(stmt);
    PTValue val = evaluate(p.expression.get());
    std::cout << formatValue(val);
    break;
  }
  case StmtType::Expr: {
    auto& e = static_cast<ExprStmt&>(stmt);
    PTValue val = evaluate(e.expression.get());
    if (replMode) std::cout << formatValue(val) << std::endl;
    break;
  }
  case StmtType::Var: {
    auto& v = static_cast<VarStmt&>(stmt);
    PTValue val;
    if (v.initializer) val = evaluate(v.initializer.get());
    defineVar(v.name, val);
    break;
  }
  case StmtType::Const: {
    auto& c = static_cast<ConstStmt&>(stmt);
    PTValue val;
    if (c.initializer) val = evaluate(c.initializer.get());
    defineVar(c.name, val);
    env->consts.insert(c.name);
    break;
  }
  case StmtType::Block: {
    auto& b = static_cast<BlockStmt&>(stmt);
    auto blockEnv = std::make_shared<Environment>(env);
    executeBlock(b.statements, blockEnv);
    break;
  }
  case StmtType::If: {
    auto& i = static_cast<IfStmt&>(stmt);
    auto cond = evaluate(i.condition.get());
    if (isTruthy(cond)) {
      execute(*i.thenBranch);
    } else if (i.elseBranch) {
      execute(*i.elseBranch);
    }
    break;
  }
  case StmtType::While: {
    auto& w = static_cast<WhileStmt&>(stmt);
    while (isTruthy(evaluate(w.condition.get()))) {
      try {
        execute(*w.body);
      } catch (const BreakException&) {
        break;
      } catch (const ContinueException&) {
        continue;
      }
    }
    break;
  }
  case StmtType::Function: {
    auto& f = static_cast<FunctionStmt&>(stmt);
    auto func = std::make_shared<PTFunction>();
    func->name = f.name;
    func->params = f.params;
    func->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>(std::move(f.body));
    func->closure = env;
    defineVar(f.name, PTValue(func));
    break;
  }
  case StmtType::Return: {
    auto& r = static_cast<ReturnStmt&>(stmt);
    PTValue val;
    if (r.value) val = evaluate(r.value.get());
    throw ReturnException(std::move(val));
  }
  case StmtType::Break:
    throw BreakException();
  case StmtType::Continue:
    throw ContinueException();
  case StmtType::Repeat: {
    auto& rp = static_cast<RepeatStmt&>(stmt);
    for (int i = 0; i < rp.count; i++) {
      try {
        auto repeatEnv = std::make_shared<Environment>(env);
        auto prev = env;
        env = repeatEnv;
        for (auto& s : rp.body) execute(*s);
        env = prev;
      } catch (const BreakException&) { break; }
        catch (const ContinueException&) { continue; }
    }
    break;
  }
  case StmtType::For: {
    auto& fr = static_cast<ForStmt&>(stmt);
    auto forEnv = std::make_shared<Environment>(env);
    auto prev = env;
    env = forEnv;
    if (fr.initializer) execute(*fr.initializer);
    while (true) {
      if (fr.condition && !isTruthy(evaluate(fr.condition.get()))) break;
      try {
        execute(*fr.body);
      } catch (const BreakException&) {
        break;
      } catch (const ContinueException&) {
        if (fr.increment) evaluate(fr.increment.get());
        continue;
      }
      if (fr.increment) evaluate(fr.increment.get());
    }
    env = prev;
    break;
  }
  case StmtType::ForEach: {
    auto& fe = static_cast<ForEachStmt&>(stmt);
    auto iterable = evaluate(fe.iterable.get());
    if (iterable.isFunction) throw PTRuntimeError("for-each requires an array or string");
    auto forEachEnv = std::make_shared<Environment>(env);
    auto prev = env;
    env = forEachEnv;
    if (iterable.isArray) {
      for (auto& elem : *iterable.array) {
        env->values[fe.variable] = elem;
        try {
          execute(*fe.body);
        } catch (const BreakException&) { break; }
        catch (const ContinueException&) { continue; }
      }
    } else {
      for (char c : iterable.value) {
        env->values[fe.variable] = PTValue(std::string(1, c));
        try {
          execute(*fe.body);
        } catch (const BreakException&) { break; }
        catch (const ContinueException&) { continue; }
      }
    }
    env = prev;
    break;
  }
  case StmtType::Try: {
    auto& ts = static_cast<TryStmt&>(stmt);
    try {
      auto tryEnv = std::make_shared<Environment>(env);
      executeBlock(ts.tryBody, tryEnv);
    } catch (const PTRuntimeError& err) {
      if (!ts.catchBody.empty()) {
        auto catchEnv = std::make_shared<Environment>(env);
        if (!ts.catchVar.empty()) catchEnv->values[ts.catchVar] = PTValue(err.what());
        executeBlock(ts.catchBody, catchEnv);
      }
    } catch (const ReturnException&) {
      if (!ts.finallyBody.empty()) {
        auto finallyEnv = std::make_shared<Environment>(env);
        executeBlock(ts.finallyBody, finallyEnv);
      }
      throw;
    }
    if (!ts.finallyBody.empty()) {
      auto finallyEnv = std::make_shared<Environment>(env);
      executeBlock(ts.finallyBody, finallyEnv);
    }
    break;
  }
  case StmtType::Import: {
    auto& imp = static_cast<ImportStmt&>(stmt);
    std::ifstream file(imp.path);
    if (!file.is_open()) throw PTRuntimeError("Could not import '" + imp.path + "'");
    std::stringstream buf;
    buf << file.rdbuf();
    std::string source = buf.str();
    Lexer lexer(source);
    auto toks = lexer.scan();
    Parser parser(toks);
    auto stmts = parser.parse();
    if (!imp.alias.empty()) {
      auto moduleEnv = std::make_shared<Environment>(globals);
      auto prevEnv = env;
      env = moduleEnv;
      for (auto& s : stmts) execute(*s);
      env = prevEnv;
      auto modMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
      for (auto& [k, v] : moduleEnv->values) (*modMap)[k] = v;
      defineVar(imp.alias, PTValue(modMap));
    } else {
      for (auto& s : stmts) execute(*s);
    }
    break;
  }
  case StmtType::Class: {
    auto& cs = static_cast<ClassStmt&>(stmt);
    auto klass = std::make_shared<PTClass>();
    klass->name = cs.name;
    klass->parentName = cs.parent;
    if (!cs.parent.empty()) {
      auto parentVal = getVar(cs.parent);
      if (!parentVal.isClass) throw PTRuntimeError("'" + cs.parent + "' is not a class");
      klass->parent = parentVal.klass;
    }
    for (auto& sm : cs.staticMethods) {
      auto func = std::make_shared<PTFunction>();
      func->name = sm->name;
      func->params = sm->params;
      func->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>(std::move(sm->body));
      func->closure = env;
      func->isStatic = true;
      klass->staticMethods[sm->name] = PTValue(func);
    }
    auto prev = env;
    auto classEnv = std::make_shared<Environment>(env);
    env = classEnv;
    for (auto& m : cs.methods) {
      auto func = std::make_shared<PTFunction>();
      func->name = m->name;
      func->params = m->params;
      func->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>(std::move(m->body));
      func->closure = env;
      if (m->name == "init") func->isInit = true;
      klass->methods[m->name] = PTValue(func);
    }
    klass->fields = std::move(cs.fields);
    env = prev;
    for (auto& [k, v] : klass->staticMethods) {
      defineVar(k, v);
    }
    defineVar(cs.name, PTValue(klass));
    break;
  }
  case StmtType::Enum: {
    auto& es = static_cast<EnumStmt&>(stmt);
    auto m = std::make_shared<std::unordered_map<std::string, PTValue>>();
    for (size_t i = 0; i < es.values.size(); i++) {
      (*m)[es.values[i]] = PTValue(static_cast<double>(i));
    }
    defineVar(es.name, PTValue(m));
    break;
  }
  case StmtType::Export: {
    auto& ex = static_cast<ExportStmt&>(stmt);
    execute(*ex.func);
    break;
  }
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
  switch (expr->type) {
  case ExprType::Literal: {
    auto* l = static_cast<Literal*>(expr);
    if (l->isNumber) return PTValue(std::stod(l->value));
    return PTValue(l->value);
  }
  case ExprType::Variable:
    return getVar(static_cast<Variable*>(expr)->name);
  case ExprType::Grouping:
    return evaluate(static_cast<Grouping*>(expr)->expression.get());
  case ExprType::ThisExpr:
    return getVar("this");
  case ExprType::SuperExpr: {
    auto* se = static_cast<SuperExpr*>(expr);
    auto thisVal = getVar("this");
    if (!thisVal.isInstance) throw PTRuntimeError("'super' can only be used in a method");
    auto instance = thisVal.instance;
    auto parent = instance->klass->parent;
    if (!parent) throw PTRuntimeError("No parent class to call super on");
    auto methodIt = parent->methods.find(se->method);
    if (methodIt == parent->methods.end()) throw PTRuntimeError("Undefined parent method '" + se->method + "'");
    auto method = methodIt->second.function;
    auto methodEnv = std::make_shared<Environment>(method->closure);
    methodEnv->values["this"] = thisVal;
    auto prev = env;
    env = methodEnv;
    PTValue result;
    try {
      for (auto& s : *method->body) execute(*s);
    } catch (const ReturnException& re) {
      result = re.value;
    }
    env = prev;
    return result;
  }
  case ExprType::TernaryExpr: {
    auto* t = static_cast<TernaryExpr*>(expr);
    if (isTruthy(evaluate(t->condition.get()))) return evaluate(t->trueBranch.get());
    return evaluate(t->falseBranch.get());
  }
  case ExprType::Unary: {
    auto* u = static_cast<Unary*>(expr);
    auto right = evaluate(u->right.get());
    if (right.isFunction || right.isArray) throw PTRuntimeError("Cannot use unary on function or array");
    if (u->op == "-") {
      if (right.isNumber) return PTValue(-right.numValue);
      return PTValue(-std::stod(right.value));
    }
    if (u->op == "!" || u->op == "not") return PTValue(!isTruthy(right));
    break;
  }
  case ExprType::PostfixExpr: {
    auto* pe = static_cast<PostfixExpr*>(expr);
    auto val = evaluate(pe->operand.get());
    if (pe->op == "++") {
      if (val.isFunction || val.isArray || val.isMap) throw PTRuntimeError("Cannot increment non-number");
      double d = toDouble(val);
      PTValue result(d + 1);
      if (auto* v = dynamic_cast<Variable*>(pe->operand.get())) assignVar(v->name, result);
      return result;
    }
    if (pe->op == "--") {
      if (val.isFunction || val.isArray || val.isMap) throw PTRuntimeError("Cannot decrement non-number");
      double d = toDouble(val);
      PTValue result(d - 1);
      if (auto* v = dynamic_cast<Variable*>(pe->operand.get())) assignVar(v->name, result);
      return result;
    }
    break;
  }
  case ExprType::ListCompExpr: {
    auto* lc = static_cast<ListCompExpr*>(expr);
    auto iterable = evaluate(lc->iterable.get());
    if (!iterable.isArray) throw PTRuntimeError("List comprehension requires an array");
    auto result = std::make_shared<std::vector<PTValue>>();
    for (auto& elem : *iterable.array) {
      auto loopEnv = std::make_shared<Environment>(env);
      auto prev = env;
      env = loopEnv;
      env->values[lc->variable] = elem;
      if (lc->condition) {
        auto cond = evaluate(lc->condition.get());
        if (isTruthy(cond)) {
          result->push_back(evaluate(lc->element.get()));
        }
      } else {
        result->push_back(evaluate(lc->element.get()));
      }
      env = prev;
    }
    return PTValue(result);
  }
  case ExprType::Binary: {
    auto* b = static_cast<Binary*>(expr);
    auto left = evaluate(b->left.get());
    auto right = evaluate(b->right.get());
    const auto& op = b->op;

    if (op == "in") {
      if (right.isArray) {
        for (auto& elem : *right.array)
          if (isEqual(elem, left)) return PTValue(true);
        return PTValue(false);
      }
      if (right.isMap) {
        return PTValue(right.map->count(left.value) ? true : false);
      }
      if (!right.isFunction) {
        return PTValue(right.value.find(left.value) != std::string::npos);
      }
      throw PTRuntimeError("'in' requires array, map, or string on right side");
    }

    if (left.isFunction || right.isFunction) throw PTRuntimeError("Cannot use binary on function");

    // Equality operators - fast path for numbers
    if (op == "==" || op == "is") {
      if (left.isNumber && right.isNumber) return PTValue(left.numValue == right.numValue);
      return PTValue(isEqual(left, right));
    }
    if (op == "!=" || op == "isnt") {
      if (left.isNumber && right.isNumber) return PTValue(left.numValue != right.numValue);
      return PTValue(!isEqual(left, right));
    }

    // Arithmetic - fast path when both are numbers
    if (left.isNumber && right.isNumber) {
      double l = left.numValue, r = right.numValue;
      if (op == "+") return PTValue(l + r);
      if (op == "-") return PTValue(l - r);
      if (op == "*") return PTValue(l * r);
      if (op == "/") { if (r == 0) throw PTRuntimeError("Division by zero"); return PTValue(l / r); }
      if (op == "%") { if (r == 0) throw PTRuntimeError("Modulo by zero"); return PTValue(std::fmod(l, r)); }
      if (op == "<") return PTValue(l < r);
      if (op == "<=") return PTValue(l <= r);
      if (op == ">") return PTValue(l > r);
      if (op == ">=") return PTValue(l >= r);
    }

    // String concat with +
    if (op == "+") {
      if (left.isArray || right.isArray) throw PTRuntimeError("Cannot add arrays with +");
      return PTValue(left.value + right.value);
    }

    // String repetition with *
    if (op == "*") {
      if ((left.isArray || right.isArray) && !(left.isArray && right.isArray))
        throw PTRuntimeError("Cannot multiply arrays");
      if (!left.isArray && !left.isMap && !left.isFunction && !right.isArray && !right.isMap && !right.isFunction) {
        if (left.isNumber && !right.isNumber) {
          std::string result;
          for (int i = 0; i < (int)left.numValue; i++) result += right.value;
          return PTValue(result);
        }
        if (!left.isNumber && right.isNumber) {
          std::string result;
          for (int i = 0; i < (int)right.numValue; i++) result += left.value;
          return PTValue(result);
        }
        if (!left.isNumber && !right.isNumber) {
          double l = std::stod(left.value), r = std::stod(right.value);
          return PTValue(l * r);
        }
      }
      return PTValue(left.numValue * right.numValue);
    }

    // Mixed-type arithmetic fallback
    if (left.isArray || right.isArray) throw PTRuntimeError("Cannot use arithmetic on arrays");
    if (left.isMap || right.isMap) throw PTRuntimeError("Cannot use arithmetic on maps");
    double l = toDouble(left), r = toDouble(right);
    if (op == "-") return PTValue(l - r);
    if (op == "/") { if (r == 0) throw PTRuntimeError("Division by zero"); return PTValue(l / r); }
    if (op == "%") { if (r == 0) throw PTRuntimeError("Modulo by zero"); return PTValue(std::fmod(l, r)); }
    if (op == "<") return PTValue(l < r);
    if (op == "<=") return PTValue(l <= r);
    if (op == ">") return PTValue(l > r);
    if (op == ">=") return PTValue(l >= r);
    break;
  }
  case ExprType::Logical: {
    auto* l = static_cast<Logical*>(expr);
    auto left = evaluate(l->left.get());
    if (l->op == "or") {
      if (isTruthy(left)) return left;
    } else {
      if (!isTruthy(left)) return left;
    }
    return evaluate(l->right.get());
  }
  case ExprType::Assign: {
    auto* a = static_cast<Assign*>(expr);
    auto val = evaluate(a->value.get());
    assignVar(a->name, val);
    return val;
  }
  case ExprType::ArrayExpr: {
    auto* ar = static_cast<ArrayExpr*>(expr);
    auto arr = std::make_shared<std::vector<PTValue>>();
    arr->reserve(ar->elements.size());
    for (auto& e : ar->elements) arr->push_back(evaluate(e.get()));
    return PTValue(arr);
  }
  case ExprType::MapExpr: {
    auto* me = static_cast<MapExpr*>(expr);
    auto m = std::make_shared<std::unordered_map<std::string, PTValue>>();
    for (auto& [k, v] : me->entries) {
      auto key = evaluate(k.get());
      if (key.isArray || key.isFunction || key.isMap) throw PTRuntimeError("Map key must be a string");
      (*m)[key.value] = evaluate(v.get());
    }
    return PTValue(m);
  }
  case ExprType::DotExpr: {
    auto* de = static_cast<DotExpr*>(expr);
    auto obj = evaluate(de->object.get());
    if (obj.isInstance) {
      auto& inst = obj.instance;
      auto it = inst->fields.find(de->name);
      if (it != inst->fields.end()) return it->second;
      auto mit = inst->klass->methods.find(de->name);
      if (mit != inst->klass->methods.end()) {
        auto method = mit->second.function;
        auto methodFunc = std::make_shared<PTFunction>();
        methodFunc->name = method->name;
        methodFunc->params = method->params;
        methodFunc->body = method->body;
        methodFunc->closure = method->closure;
        auto methodEnv = std::make_shared<Environment>(methodFunc->closure);
        methodEnv->values["this"] = obj;
        methodFunc->closure = methodEnv;
        return PTValue(methodFunc);
      }
      if (inst->klass->parent) {
        auto pit = inst->klass->parent->methods.find(de->name);
        if (pit != inst->klass->parent->methods.end()) {
          auto method = pit->second.function;
          auto methodFunc = std::make_shared<PTFunction>();
          methodFunc->name = method->name;
          methodFunc->params = method->params;
          methodFunc->body = method->body;
          methodFunc->closure = method->closure;
          auto methodEnv = std::make_shared<Environment>(methodFunc->closure);
          methodEnv->values["this"] = obj;
          methodFunc->closure = methodEnv;
          return PTValue(methodFunc);
        }
      }
      throw PTRuntimeError("Undefined property '" + de->name + "'");
    }
    if (obj.isClass) {
      auto sit = obj.klass->staticMethods.find(de->name);
      if (sit != obj.klass->staticMethods.end()) return sit->second;
      auto mit = obj.klass->methods.find(de->name);
      if (mit != obj.klass->methods.end()) return mit->second;
      throw PTRuntimeError("Undefined property '" + de->name + "'");
    }
    if (obj.isMap) {
      auto it = obj.map->find(de->name);
      if (it == obj.map->end()) throw PTRuntimeError("Undefined property '" + de->name + "'");
      return it->second;
    }
    throw PTRuntimeError("Cannot access property on non-map, class, or instance");
  }
  case ExprType::DotAssignExpr: {
    auto* da = static_cast<DotAssignExpr*>(expr);
    auto obj = evaluate(da->object.get());
    if (obj.isInstance) {
      auto val = evaluate(da->value.get());
      obj.instance->fields[da->name] = val;
      return val;
    }
    if (!obj.isMap) throw PTRuntimeError("Cannot assign property on non-map");
    auto val = evaluate(da->value.get());
    (*obj.map)[da->name] = val;
    return val;
  }
  case ExprType::IndexExpr: {
    auto* ix = static_cast<IndexExpr*>(expr);
    auto callee = evaluate(ix->callee.get());
    auto idx = evaluate(ix->index.get());
    if (callee.isMap) {
      if (idx.isArray || idx.isFunction || idx.isMap) throw PTRuntimeError("Map key must be a string");
      auto it = callee.map->find(idx.value);
      if (it == callee.map->end()) throw PTRuntimeError("Undefined key '" + idx.value + "'");
      return it->second;
    }
    if (idx.isArray || idx.isFunction) throw PTRuntimeError("Index must be a number");
    int i = idx.isNumber ? (int)idx.numValue : (int)std::stod(idx.value);
    if (callee.isArray) {
      int size = (int)callee.array->size();
      if (i < 0) i += size;
      if (i < 0 || i >= size) throw PTRuntimeError("Index out of bounds");
      return (*callee.array)[i];
    }
    if (!callee.isFunction && !callee.isArray) {
      int size = (int)callee.value.size();
      if (i < 0) i += size;
      if (i < 0 || i >= size) throw PTRuntimeError("String index out of bounds");
      return PTValue(std::string(1, callee.value[i]));
    }
    throw PTRuntimeError("Cannot index into this value");
  }
  case ExprType::AssignIndex: {
    auto* ai = static_cast<AssignIndex*>(expr);
    auto callee = evaluate(ai->callee.get());
    if (!callee.isArray) throw PTRuntimeError("Can only assign index into arrays");
    auto idx = evaluate(ai->index.get());
    if (idx.isArray || idx.isFunction) throw PTRuntimeError("Array index must be a number");
    int i = idx.isNumber ? (int)idx.numValue : (int)std::stod(idx.value);
    int size = (int)callee.array->size();
    if (i < 0) i += size;
    if (i < 0 || i >= size) throw PTRuntimeError("Index out of bounds");
    auto val = evaluate(ai->value.get());
    (*callee.array)[i] = val;
    return val;
  }
  case ExprType::Call: {
    auto* c = static_cast<Call*>(expr);
    // Fast builtin check
    if (c->callee->type == ExprType::Variable) {
      auto* var = static_cast<Variable*>(c->callee.get());
      if (!varExists(var->name)) {
        PTValue result = callBuiltin(var->name, c->arguments);
        if (!result.isFunction) return result;
      }
    }
    auto callee = evaluate(c->callee.get());
    if (callee.isClass) {
      auto instance = std::make_shared<PTInstance>();
      instance->klass = callee.klass;
      auto instanceVal = PTValue(instance);
      std::vector<PTClass*> chain;
      for (auto* k = callee.klass.get(); k; k = k->parent.get()) chain.push_back(k);
      for (int ci = (int)chain.size() - 1; ci >= 0; ci--) {
        for (auto& [fname, fexpr] : chain[ci]->fields) {
          if (fexpr) instance->fields[fname] = evaluate(fexpr.get());
          else instance->fields[fname] = PTValue();
        }
      }
      PTFunction* initFunc = nullptr;
      for (auto* k : chain) {
        auto it = k->methods.find("init");
        if (it != k->methods.end()) { initFunc = it->second.function.get(); break; }
      }
      if (initFunc) {
        auto callEnv = std::make_shared<Environment>(initFunc->closure);
        callEnv->values["this"] = instanceVal;
        auto prev = env;
        env = callEnv;
        for (size_t i = 0; i < initFunc->params.size(); i++) {
          PTValue argVal;
          if (i < c->arguments.size()) argVal = evaluate(c->arguments[i].get());
          callEnv->values[initFunc->params[i]] = argVal;
        }
        try { for (auto& s : *initFunc->body) execute(*s); }
        catch (const ReturnException&) {}
        env = prev;
      }
      return instanceVal;
    }
    if (!callee.isFunction) throw PTRuntimeError("Can only call functions");
    auto& fn = callee.function;
    size_t argCount = c->arguments.size();
    size_t paramCount = fn->params.size();
    if (argCount != paramCount) throw PTRuntimeError("Expected " + std::to_string(paramCount) + " arguments but got " + std::to_string(argCount));
    auto callEnv = std::make_shared<Environment>(fn->closure);
    for (size_t i = 0; i < fn->params.size(); i++) {
      callEnv->values[fn->params[i]] = evaluate(c->arguments[i].get());
    }
    auto prev = env;
    env = callEnv;
    PTValue result;
    try {
      for (auto& stmt : *fn->body) execute(*stmt);
    } catch (const ReturnException& re) {
      result = re.value;
    }
    env = prev;
    return result;
  }
  case ExprType::ThrowExpr: {
    auto* te = static_cast<ThrowExpr*>(expr);
    throw PTRuntimeError(evaluate(te->value.get()).value);
  }
  case ExprType::LambdaExpr: {
    auto* le = static_cast<LambdaExpr*>(expr);
    auto func = std::make_shared<PTFunction>();
    func->params = le->params;
    func->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>(std::move(le->body));
    func->closure = env;
    return PTValue(func);
  }
  case ExprType::InterpolatedExpr: {
    auto* ie = static_cast<InterpolatedExpr*>(expr);
    std::string result;
    for (size_t i = 0; i < ie->strings.size(); i++) {
      result += ie->strings[i];
      if (i < ie->exprs.size()) result += formatValue(evaluate(ie->exprs[i].get()));
    }
    return PTValue(result);
  }
  case ExprType::MatchExpr: {
    auto* me = static_cast<MatchExpr*>(expr);
    auto value = evaluate(me->value.get());
    for (auto& mc : me->cases) {
      for (auto& pattern : mc->patterns) {
        if (pattern->type == ExprType::Variable) {
          if (static_cast<Variable*>(pattern.get())->name == "_") {
            auto matchEnv = std::make_shared<Environment>(env);
            auto prev = env;
            env = matchEnv;
            auto result = evaluate(mc->body.get());
            env = prev;
            return result;
          }
        }
        auto patVal = evaluate(pattern.get());
        if (isEqual(value, patVal)) {
          auto matchEnv = std::make_shared<Environment>(env);
          auto prev = env;
          env = matchEnv;
          auto result = evaluate(mc->body.get());
          env = prev;
          return result;
        }
      }
    }
    throw PTRuntimeError("No matching case in match expression");
  }
  }
  throw PTRuntimeError("Unknown expression");
}

PTValue Interpreter::evaluateFunction(const PTValue& fnVal, const std::vector<PTValue>& args) {
  if (!fnVal.isFunction) throw PTRuntimeError("Can only call functions");
  auto& fn = fnVal.function;
  if (args.size() != fn->params.size())
    throw PTRuntimeError("Expected " + std::to_string(fn->params.size()) + " arguments but got " + std::to_string(args.size()));
  auto funcEnv = std::make_shared<Environment>(fn->closure);
  for (size_t i = 0; i < fn->params.size(); i++)
    funcEnv->values[fn->params[i]] = args[i];
  auto prev = env;
  env = funcEnv;
  PTValue result;
  try {
    for (auto& stmt : *fn->body) execute(*stmt);
  } catch (const ReturnException& re) {
    result = re.value;
  }
  env = prev;
  return result;
}

PTValue Interpreter::callBuiltin(const std::string& name, const std::vector<std::unique_ptr<Expr>>& args) {
  if (name == "len") {
    if (args.size() != 1) throw PTRuntimeError("len() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isFunction) throw PTRuntimeError("len() expects a string or array");
    if (arg.isArray) return PTValue(static_cast<double>(arg.array->size()));
    return PTValue(static_cast<double>(arg.value.size()));
  }
  if (name == "push") {
    if (args.size() != 2) throw PTRuntimeError("push() expects 2 arguments");
    auto arr = evaluate(args[0].get());
    if (!arr.isArray) throw PTRuntimeError("push() expects an array");
    arr.array->push_back(evaluate(args[1].get()));
    return PTValue(true);
  }
  if (name == "pop") {
    if (args.size() != 1) throw PTRuntimeError("pop() expects 1 argument");
    auto arr = evaluate(args[0].get());
    if (!arr.isArray) throw PTRuntimeError("pop() expects an array");
    if (arr.array->empty()) return PTValue("nil");
    auto val = arr.array->back();
    arr.array->pop_back();
    return val;
  }
  if (name == "toNum") {
    if (args.size() != 1) throw PTRuntimeError("toNum() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isFunction || arg.isArray) throw PTRuntimeError("toNum() expects a string");
    try { return PTValue(std::stod(arg.value)); }
    catch (...) { return PTValue("nil"); }
  }
  if (name == "toString") {
    if (args.size() != 1) throw PTRuntimeError("toString() expects 1 argument");
    return PTValue(formatValue(evaluate(args[0].get())));
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
    if (args.size() != 1) throw PTRuntimeError("readFile() expects 1 argument");
    auto path = evaluate(args[0].get());
    std::ifstream file(path.value);
    if (!file.is_open()) return PTValue("nil");
    std::stringstream buf;
    buf << file.rdbuf();
    return PTValue(buf.str());
  }
  if (name == "writeFile") {
    if (args.size() != 2) throw PTRuntimeError("writeFile() expects 2 arguments");
    auto path = evaluate(args[0].get());
    auto content = evaluate(args[1].get());
    std::ofstream file(path.value);
    if (!file.is_open()) return PTValue(false);
    file << content.value;
    return PTValue(true);
  }
  if (name == "abs") {
    if (args.size() != 1) throw PTRuntimeError("abs() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("abs() expects a number");
    return PTValue(std::fabs(toDouble(arg)));
  }
  if (name == "sqrt") {
    if (args.size() != 1) throw PTRuntimeError("sqrt() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("sqrt() expects a number");
    return PTValue(std::sqrt(toDouble(arg)));
  }
  if (name == "min") {
    if (args.size() != 2) throw PTRuntimeError("min() expects 2 arguments");
    auto a = evaluate(args[0].get());
    auto b = evaluate(args[1].get());
    if (a.isArray || a.isFunction || b.isArray || b.isFunction) throw PTRuntimeError("min() expects numbers");
    double da = toDouble(a), db = toDouble(b);
    return PTValue(da < db ? da : db);
  }
  if (name == "max") {
    if (args.size() != 2) throw PTRuntimeError("max() expects 2 arguments");
    auto a = evaluate(args[0].get());
    auto b = evaluate(args[1].get());
    if (a.isArray || a.isFunction || b.isArray || b.isFunction) throw PTRuntimeError("max() expects numbers");
    double da = toDouble(a), db = toDouble(b);
    return PTValue(da > db ? da : db);
  }
  if (name == "floor") {
    if (args.size() != 1) throw PTRuntimeError("floor() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("floor() expects a number");
    return PTValue(std::floor(toDouble(arg)));
  }
  if (name == "ceil") {
    if (args.size() != 1) throw PTRuntimeError("ceil() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("ceil() expects a number");
    return PTValue(std::ceil(toDouble(arg)));
  }
  if (name == "round") {
    if (args.size() != 1) throw PTRuntimeError("round() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("round() expects a number");
    return PTValue(std::round(toDouble(arg)));
  }
  if (name == "type") {
    if (args.size() != 1) throw PTRuntimeError("type() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isFunction) return PTValue("function");
    if (arg.isArray) return PTValue("array");
    if (arg.isMap) return PTValue("map");
    if (arg.isClass) return PTValue("class");
    if (arg.isInstance) return PTValue("instance");
    if (arg.value == "nil") return PTValue("nil");
    if (arg.isNumber) return PTValue("number");
    return PTValue("string");
  }
  if (name == "keys") {
    if (args.size() != 1) throw PTRuntimeError("keys() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (!arg.isMap) throw PTRuntimeError("keys() expects a map");
    auto arr = std::make_shared<std::vector<PTValue>>();
    for (auto& [k, v] : *arg.map) arr->push_back(PTValue(k));
    return PTValue(arr);
  }
  if (name == "values") {
    if (args.size() != 1) throw PTRuntimeError("values() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (!arg.isMap) throw PTRuntimeError("values() expects a map");
    auto arr = std::make_shared<std::vector<PTValue>>();
    for (auto& [k, v] : *arg.map) arr->push_back(v);
    return PTValue(arr);
  }
  if (name == "has") {
    if (args.size() != 2) throw PTRuntimeError("has() expects 2 arguments");
    auto obj = evaluate(args[0].get());
    auto key = evaluate(args[1].get());
    if (!obj.isMap) throw PTRuntimeError("has() expects a map");
    if (key.isArray || key.isFunction || key.isMap) throw PTRuntimeError("has() key must be a string");
    return PTValue(obj.map->count(key.value) ? true : false);
  }
  if (name == "upper") {
    if (args.size() != 1) throw PTRuntimeError("upper() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("upper() expects a string");
    std::string s = arg.value;
    for (auto& c : s) c = std::toupper(c);
    return PTValue(s);
  }
  if (name == "lower") {
    if (args.size() != 1) throw PTRuntimeError("lower() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("lower() expects a string");
    std::string s = arg.value;
    for (auto& c : s) c = std::tolower(c);
    return PTValue(s);
  }
  if (name == "trim") {
    if (args.size() != 1) throw PTRuntimeError("trim() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("trim() expects a string");
    std::string s = arg.value;
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return PTValue("");
    size_t end = s.find_last_not_of(" \t\n\r");
    return PTValue(s.substr(start, end - start + 1));
  }
  if (name == "substr") {
    if (args.size() < 2 || args.size() > 3) throw PTRuntimeError("substr() expects 2 or 3 arguments");
    auto str = evaluate(args[0].get());
    if (str.isArray || str.isFunction) throw PTRuntimeError("substr() expects a string");
    auto startArg = evaluate(args[1].get());
    int start = startArg.isNumber ? (int)startArg.numValue : (int)std::stod(startArg.value);
    int len = (int)str.value.size() - start;
    if (args.size() == 3) {
      auto lenArg = evaluate(args[2].get());
      len = lenArg.isNumber ? (int)lenArg.numValue : (int)std::stod(lenArg.value);
    }
    if (start < 0 || start >= (int)str.value.size()) throw PTRuntimeError("substr() start out of bounds");
    if (start + len > (int)str.value.size()) len = (int)str.value.size() - start;
    return PTValue(str.value.substr(start, len));
  }
  if (name == "contains") {
    if (args.size() != 2) throw PTRuntimeError("contains() expects 2 arguments");
    auto str = evaluate(args[0].get());
    auto sub = evaluate(args[1].get());
    if (str.isArray || str.isFunction) throw PTRuntimeError("contains() expects a string");
    if (sub.isArray || sub.isFunction) throw PTRuntimeError("contains() expects a string");
    return PTValue(str.value.find(sub.value) != std::string::npos);
  }
  if (name == "replace") {
    if (args.size() != 3) throw PTRuntimeError("replace() expects 3 arguments");
    auto str = evaluate(args[0].get());
    auto old = evaluate(args[1].get());
    auto newStr = evaluate(args[2].get());
    if (str.isArray || str.isFunction) throw PTRuntimeError("replace() expects a string");
    if (old.isArray || old.isFunction) throw PTRuntimeError("replace() expects a string");
    if (newStr.isArray || newStr.isFunction) throw PTRuntimeError("replace() expects a string");
    std::string result = str.value;
    size_t pos = 0;
    while ((pos = result.find(old.value, pos)) != std::string::npos) {
      result.replace(pos, old.value.size(), newStr.value);
      pos += newStr.value.size();
    }
    return PTValue(result);
  }
  if (name == "split") {
    if (args.size() != 2) throw PTRuntimeError("split() expects 2 arguments");
    auto str = evaluate(args[0].get());
    auto delim = evaluate(args[1].get());
    if (str.isArray || str.isFunction) throw PTRuntimeError("split() expects a string");
    if (delim.isArray || delim.isFunction) throw PTRuntimeError("split() expects a string");
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
    if (args.size() < 1 || args.size() > 2) throw PTRuntimeError("assert() expects 1 or 2 arguments");
    auto cond = evaluate(args[0].get());
    if (!isTruthy(cond)) {
      std::string msg = "Assertion failed";
      if (args.size() == 2) msg = evaluate(args[1].get()).value;
      throw PTRuntimeError(msg);
    }
    return PTValue(true);
  }
  if (name == "join") {
    if (args.size() != 2) throw PTRuntimeError("join() expects 2 arguments");
    auto arr = evaluate(args[0].get());
    auto delim = evaluate(args[1].get());
    if (!arr.isArray) throw PTRuntimeError("join() expects an array");
    if (delim.isArray || delim.isFunction) throw PTRuntimeError("join() delimiter must be a string");
    std::string result;
    for (size_t i = 0; i < arr.array->size(); i++) {
      if (i > 0) result += delim.value;
      result += formatValue((*arr.array)[i]);
    }
    return PTValue(result);
  }
  if (name == "indexOf") {
    if (args.size() != 2) throw PTRuntimeError("indexOf() expects 2 arguments");
    auto haystack = evaluate(args[0].get());
    auto needle = evaluate(args[1].get());
    if (haystack.isArray) {
      for (size_t i = 0; i < haystack.array->size(); i++)
        if (isEqual((*haystack.array)[i], needle))
          return PTValue(static_cast<double>(i));
      return PTValue(-1.0);
    }
    if (haystack.isFunction) throw PTRuntimeError("indexOf() expects a string or array");
    if (needle.isArray || needle.isFunction) throw PTRuntimeError("indexOf() needle must be a string");
    auto pos = haystack.value.find(needle.value);
    if (pos == std::string::npos) return PTValue(-1.0);
    return PTValue(static_cast<double>(pos));
  }
  if (name == "sort") {
    if (args.size() != 1) throw PTRuntimeError("sort() expects 1 argument");
    auto arr = evaluate(args[0].get());
    if (!arr.isArray) throw PTRuntimeError("sort() expects an array");
    auto sorted = std::make_shared<std::vector<PTValue>>(*arr.array);
    std::sort(sorted->begin(), sorted->end(), [](const PTValue& a, const PTValue& b) {
      if (a.isNumber && b.isNumber) return a.numValue < b.numValue;
      return a.value < b.value;
    });
    return PTValue(sorted);
  }
  if (name == "range") {
    if (args.size() < 1 || args.size() > 2) throw PTRuntimeError("range() expects 1 or 2 arguments");
    auto arr = std::make_shared<std::vector<PTValue>>();
    if (args.size() == 1) {
      int end = (int)toDouble(evaluate(args[0].get()));
      for (int i = 0; i < end; i++) arr->push_back(PTValue(static_cast<double>(i)));
    } else {
      int start = (int)toDouble(evaluate(args[0].get()));
      int end = (int)toDouble(evaluate(args[1].get()));
      if (start <= end) for (int i = start; i < end; i++) arr->push_back(PTValue(static_cast<double>(i)));
      else for (int i = start; i > end; i--) arr->push_back(PTValue(static_cast<double>(i)));
    }
    return PTValue(arr);
  }
  if (name == "map") {
    if (args.size() != 2) throw PTRuntimeError("map() expects 2 arguments");
    auto arr = evaluate(args[0].get());
    auto fn = evaluate(args[1].get());
    if (!arr.isArray) throw PTRuntimeError("map() expects an array");
    if (!fn.isFunction) throw PTRuntimeError("map() expects a function");
    auto result = std::make_shared<std::vector<PTValue>>();
    for (auto& elem : *arr.array) result->push_back(evaluateFunction(fn, {elem}));
    return PTValue(result);
  }
  if (name == "filter") {
    if (args.size() != 2) throw PTRuntimeError("filter() expects 2 arguments");
    auto arr = evaluate(args[0].get());
    auto fn = evaluate(args[1].get());
    if (!arr.isArray) throw PTRuntimeError("filter() expects an array");
    if (!fn.isFunction) throw PTRuntimeError("filter() expects a function");
    auto result = std::make_shared<std::vector<PTValue>>();
    for (auto& elem : *arr.array) {
      if (isTruthy(evaluateFunction(fn, {elem}))) result->push_back(elem);
    }
    return PTValue(result);
  }
  if (name == "reduce") {
    if (args.size() < 2 || args.size() > 3) throw PTRuntimeError("reduce() expects 2 or 3 arguments");
    auto arr = evaluate(args[0].get());
    auto fn = evaluate(args[1].get());
    if (!arr.isArray) throw PTRuntimeError("reduce() expects an array");
    if (!fn.isFunction) throw PTRuntimeError("reduce() expects a function");
    PTValue acc;
    size_t start = 0;
    if (args.size() == 3) {
      acc = evaluate(args[2].get());
    } else {
      if (arr.array->empty()) throw PTRuntimeError("reduce() requires initial value for empty array");
      acc = (*arr.array)[0];
      start = 1;
    }
    for (size_t i = start; i < arr.array->size(); i++) {
      acc = evaluateFunction(fn, {acc, (*arr.array)[i]});
    }
    return acc;
  }
  if (name == "random") {
    if (args.size() > 2) throw PTRuntimeError("random() expects 0, 1, or 2 arguments");
    static std::mt19937 rng(std::random_device{}());
    if (args.size() == 0) {
      std::uniform_real_distribution<double> dist(0.0, 1.0);
      return PTValue(dist(rng));
    }
    if (args.size() == 1) {
      int max = (int)toDouble(evaluate(args[0].get()));
      std::uniform_int_distribution<int> dist(0, max - 1);
      return PTValue(static_cast<double>(dist(rng)));
    }
    int min = (int)toDouble(evaluate(args[0].get()));
    int max = (int)toDouble(evaluate(args[1].get()));
    std::uniform_int_distribution<int> dist(min, max - 1);
    return PTValue(static_cast<double>(dist(rng)));
  }
  if (name == "clock") {
    if (args.size() != 0) throw PTRuntimeError("clock() expects no arguments");
    auto now = std::chrono::steady_clock::now();
    return PTValue(std::chrono::duration<double>(now.time_since_epoch()).count());
  }
  if (name == "getenv") {
    if (args.size() != 1) throw PTRuntimeError("getenv() expects 1 argument");
    auto key = evaluate(args[0].get());
    const char* val = std::getenv(key.value.c_str());
    return PTValue(val ? std::string(val) : "nil");
  }
  if (name == "fileExists") {
    if (args.size() != 1) throw PTRuntimeError("fileExists() expects 1 argument");
    auto path = evaluate(args[0].get());
    std::ifstream f(path.value);
    return PTValue(f.good());
  }
  if (name == "sqliteOpen") {
    if (args.size() != 1) throw PTRuntimeError("sqliteOpen() expects 1 argument");
    auto path = evaluate(args[0].get());
    sqlite3* db;
    if (sqlite3_open(path.value.c_str(), &db) != SQLITE_OK) {
      std::string err = sqlite3_errmsg(db);
      sqlite3_close(db);
      throw PTRuntimeError("Cannot open database: " + err);
    }
    return PTValue(db);
  }
  if (name == "sqliteExec") {
    if (args.size() != 2) throw PTRuntimeError("sqliteExec(db, sql) expects 2 arguments");
    auto dbArg = evaluate(args[0].get());
    auto sql = evaluate(args[1].get());
    if (!dbArg.isDatabase) throw PTRuntimeError("sqliteExec() expects a database");
    char* errMsg = nullptr;
    if (sqlite3_exec(dbArg.db, sql.value.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
      std::string err = errMsg;
      sqlite3_free(errMsg);
      throw PTRuntimeError("SQL error: " + err);
    }
    return PTValue(true);
  }
  if (name == "sqliteQuery") {
    if (args.size() != 2) throw PTRuntimeError("sqliteQuery(db, sql) expects 2 arguments");
    auto dbArg = evaluate(args[0].get());
    auto sql = evaluate(args[1].get());
    if (!dbArg.isDatabase) throw PTRuntimeError("sqliteQuery() expects a database");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(dbArg.db, sql.value.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
      throw PTRuntimeError("SQL error: " + std::string(sqlite3_errmsg(dbArg.db)));
    }
    auto rows = std::make_shared<std::vector<PTValue>>();
    int colCount = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      auto row = std::make_shared<std::unordered_map<std::string, PTValue>>();
      for (int i = 0; i < colCount; i++) {
        std::string colName = sqlite3_column_name(stmt, i);
        const unsigned char* text = sqlite3_column_text(stmt, i);
        if (text) (*row)[colName] = PTValue(std::string(reinterpret_cast<const char*>(text)));
        else (*row)[colName] = PTValue("nil");
      }
      rows->push_back(PTValue(row));
    }
    sqlite3_finalize(stmt);
    return PTValue(rows);
  }
  if (name == "sqliteClose") {
    if (args.size() != 1) throw PTRuntimeError("sqliteClose() expects 1 argument");
    auto dbArg = evaluate(args[0].get());
    if (!dbArg.isDatabase) throw PTRuntimeError("sqliteClose() expects a database");
    sqlite3_close(dbArg.db);
    return PTValue(true);
  }
  if (name == "httpListen") {
    if (args.size() != 2) throw PTRuntimeError("httpListen(port, handler) expects 2 arguments");
    auto portVal = evaluate(args[0].get());
    int port = portVal.isNumber ? (int)portVal.numValue : (int)std::stod(portVal.value);
    auto handler = evaluate(args[1].get());
    if (!handler.isFunction) throw PTRuntimeError("httpListen() second argument must be a function");
    int server_fd = httpCreateServer(port);
    if (server_fd < 0) throw PTRuntimeError("Could not create server on port " + std::to_string(port));
    std::cout << "PT Server running at http://localhost:" << port << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;
    while (true) {
      struct sockaddr_in clientAddr{};
      socklen_t clientLen = sizeof(clientAddr);
      int client_fd = accept(server_fd, (struct sockaddr*)&clientAddr, &clientLen);
      if (client_fd < 0) continue;
      std::string rawReq = httpReadRequest(client_fd);
      HttpRequest req = httpParseRequest(rawReq);
      auto reqMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
      (*reqMap)["method"] = PTValue(req.method);
      (*reqMap)["path"] = PTValue(req.path);
      (*reqMap)["body"] = PTValue(req.body);
      auto hdrMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
      for (auto& [k, v] : req.headers) (*hdrMap)[k] = PTValue(v);
      (*reqMap)["headers"] = PTValue(hdrMap);
      std::string method = req.method;
      std::string path = req.path;
      std::cout << method << " " << path << std::endl;
      auto savedEnv = env;
      auto handlerEnv = std::make_shared<Environment>(handler.function->closure);
      handlerEnv->values["req"] = PTValue(reqMap);
      env = handlerEnv;
      PTValue response;
      try {
        for (auto& s : *handler.function->body) execute(*s);
      } catch (const ReturnException& re) {
        response = re.value;
      } catch (const PTRuntimeError& err) {
        env = savedEnv;
        std::string errMsg = err.what();
        std::string errorHtml = "<html><body><h1>500 Internal Server Error</h1><p>" + errMsg + "</p></body></html>";
        httpSendResponse(client_fd, 500, "Internal Server Error", "text/html", errorHtml);
        httpClose(client_fd);
        continue;
      }
      env = savedEnv;
      int status = 200;
      std::string statusText = "OK";
      std::string contentType = "text/html";
      std::string body;
      if (response.isMap) {
        if (response.map->count("status")) {
          status = response.map->count("status") ? (int)toDouble((*response.map)["status"]) : 200;
          if (status == 404) statusText = "Not Found";
          else if (status == 500) statusText = "Internal Server Error";
          else if (status == 301) statusText = "Moved Permanently";
          else if (status == 201) statusText = "Created";
          else statusText = "OK";
        }
        if (response.map->count("headers")) {
          auto& respHeaders = (*response.map)["headers"];
          if (respHeaders.isMap && respHeaders.map->count("content-type"))
            contentType = (*respHeaders.map)["content-type"].value;
        }
        if (response.map->count("body")) body = (*response.map)["body"].value;
        else if (response.map->count("html")) body = (*response.map)["html"].value;
      } else {
        body = formatValue(response);
      }
      httpSendResponse(client_fd, status, statusText, contentType, body);
      httpClose(client_fd);
    }
    httpClose(server_fd);
    return PTValue();
  }
  return PTValue(std::make_shared<PTFunction>());
}

bool Interpreter::isTruthy(const PTValue& val) {
  if (val.isNumber) return val.numValue != 0;
  if (val.isFunction) return true;
  if (val.isArray) return val.array->size() > 0;
  if (val.isMap) return val.map->size() > 0;
  if (val.isClass || val.isInstance) return true;
  if (val.isDatabase) return true;
  return val.value != "false" && val.value != "nil" && val.value != "0" && val.value != "";
}

bool Interpreter::isEqual(const PTValue& a, const PTValue& b) {
  if (a.isNumber && b.isNumber) return a.numValue == b.numValue;
  if (a.isFunction || b.isFunction) return false;
  if (a.isArray || b.isArray) {
    if (!a.isArray || !b.isArray) return false;
    if (a.array->size() != b.array->size()) return false;
    for (size_t i = 0; i < a.array->size(); i++)
      if (!isEqual((*a.array)[i], (*b.array)[i])) return false;
    return true;
  }
  if (a.isMap || b.isMap) {
    if (!a.isMap || !b.isMap) return false;
    if (a.map->size() != b.map->size()) return false;
    for (auto& [k, v] : *a.map) {
      if (!b.map->count(k)) return false;
      if (!isEqual(v, (*b.map)[k])) return false;
    }
    return true;
  }
  return a.value == b.value;
}
