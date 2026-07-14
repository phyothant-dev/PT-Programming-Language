#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "http.h"
#include "json.h"
#include "ptcurl.h"
#include "crypto.h"
#ifdef HAS_PG
#include "pg.h"
#endif
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
#include <thread>
#include <mutex>
#include <atomic>

static const PTValue PT_TRUE(true);
static const PTValue PT_FALSE(false);
static const PTValue PT_NIL;

void Interpreter::defineVar(int id, PTValue value) {
  env->set(id, std::move(value));
}

void Interpreter::assignVar(int id, const PTValue& value) {
  auto e = env;
  while (e) {
    for (size_t i = 0; i < e->values.size(); i++) {
      if (e->values[i].first == id) {
        for (auto& c : e->consts) { if (c == id) throw PTRuntimeError("Cannot reassign constant '" + interner.name(id) + "'"); }
        e->values[i].second = value;
        return;
      }
    }
    e = e->enclosing;
  }
  throw PTRuntimeError("Undefined variable '" + interner.name(id) + "'");
}

const PTValue& Interpreter::getVar(int id) {
  auto e = env;
  while (e) {
    PTValue* f = e->find(id);
    if (f) return *f;
    e = e->enclosing;
  }
  throw PTRuntimeError("Undefined variable '" + interner.name(id) + "'");
}

const PTValue* Interpreter::findVar(int id) {
  auto e = env;
  while (e) {
    PTValue* f = e->find(id);
    if (f) return f;
    e = e->enclosing;
  }
  return nullptr;
}

bool Interpreter::varExists(int id) {
  auto e = env;
  while (e) {
    if (e->find(id)) return true;
    e = e->enclosing;
  }
  return false;
}

void Interpreter::interpret(std::vector<std::unique_ptr<Stmt>>& stmts) {
  if (!globals) {
    globals = std::make_shared<Environment>();
    env = globals;
  }
  for (auto& stmt : stmts) {
    execute(*stmt);
    if (returning) break;
  }
}

std::string Interpreter::formatValue(const PTValue& val) {
  switch (val.type) {
  case PTValue::TNumber: {
    double n = val.numValue;
    if (n == static_cast<long long>(n) && n >= -1e15 && n <= 1e15)
      return std::to_string(static_cast<long long>(n));
    char buf[64];
    snprintf(buf, sizeof(buf), "%.6g", n);
    return buf;
  }
  case PTValue::TBool: return val.boolValue ? "true" : "false";
  case PTValue::TNil: return "nil";
  case PTValue::TString: return val.value;
  case PTValue::TFunction:
    if (val.function && !val.function->name.empty()) return "<fn " + val.function->name + ">";
    return "<fn>";
  case PTValue::TClass: return "<class " + val.klass->name + ">";
  case PTValue::TInstance: return "<instance of " + val.instance->klass->name + ">";
  case PTValue::TArray: {
    std::string s = "[";
    for (size_t i = 0; i < val.array->size(); i++) {
      if (i > 0) s += ", ";
      s += formatValue((*val.array)[i]);
    }
    return s + "]";
  }
  case PTValue::TMap: {
    std::string s = "{";
    bool first = true;
    for (auto& [k, v] : *val.map) {
      if (!first) s += ", ";
      s += k + ": " + formatValue(v);
      first = false;
    }
    return s + "}";
  }
  case PTValue::TDatabase: return "<database>";
  }
  return val.value;
}

static inline double toDouble(const PTValue& v) {
  if (v.isNumber()) return v.numValue;
  return std::stod(v.value);
}

void Interpreter::execute(Stmt& stmt) {
  if (returning) return;
  switch (stmt.stype) {
  case StmtType::Print: {
    auto& p = static_cast<PrintStmt&>(stmt);
    std::cout << formatValue(evaluate(p.expression.get())) << std::endl;
    break;
  }
  case StmtType::PrintNL: {
    auto& p = static_cast<PrintNLStmt&>(stmt);
    std::cout << formatValue(evaluate(p.expression.get()));
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
    defineVar(internCached(v.name, v.id), v.initializer ? evaluate(v.initializer.get()) : PTValue());
    break;
  }
  case StmtType::Const: {
    auto& c = static_cast<ConstStmt&>(stmt);
    int id = internCached(c.name, c.id);
    defineVar(id, c.initializer ? evaluate(c.initializer.get()) : PTValue());
    env->addConst(id);
    break;
  }
  case StmtType::Block: {
    auto& b = static_cast<BlockStmt&>(stmt);
    executeBlock(b.statements, acquireEnv(env));
    break;
  }
  case StmtType::If: {
    auto& i = static_cast<IfStmt&>(stmt);
    if (isTruthy(evaluate(i.condition.get())))
      execute(*i.thenBranch);
    else if (i.elseBranch)
      execute(*i.elseBranch);
    break;
  }
  case StmtType::While: {
    auto& w = static_cast<WhileStmt&>(stmt);
    if (w.body->stype == StmtType::Block) {
      auto& body = static_cast<BlockStmt&>(*w.body);
      auto loopEnv = acquireEnv(env);
      auto prev = env;
      while (!returning) {
        if (!isTruthy(evaluate(w.condition.get()))) break;
        env = loopEnv;
        for (auto& s : body.statements) {
          execute(*s);
          if (returning || breaking || continuing) break;
        }
        if (breaking) { breaking = false; break; }
        if (continuing) { continuing = false; }
      }
      env = prev;
    } else {
      while (!returning) {
        if (!isTruthy(evaluate(w.condition.get()))) break;
        execute(*w.body);
        if (breaking) { breaking = false; break; }
        if (continuing) { continuing = false; }
      }
    }
    break;
  }
  case StmtType::Function: {
    auto& f = static_cast<FunctionStmt&>(stmt);
    auto func = std::make_shared<PTFunction>();
    func->name = f.name;
    func->params = f.params;
    func->paramIds.resize(f.params.size());
    for (size_t i = 0; i < f.params.size(); i++)
      func->paramIds[i] = interner.intern(f.params[i]);
    func->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>(std::move(f.body));
    func->closure = env;
    defineVar(internCached(f.name, f.id), PTValue(func));
    break;
  }
  case StmtType::Return: {
    auto& r = static_cast<ReturnStmt&>(stmt);
    if (r.value) returnValue = evaluate(r.value.get());
    returning = true;
    break;
  }
  case StmtType::Break:
    breaking = true;
    break;
  case StmtType::Continue:
    continuing = true;
    break;
  case StmtType::Repeat: {
    auto& rp = static_cast<RepeatStmt&>(stmt);
    auto repeatEnv = acquireEnv(env);
    for (int i = 0; i < rp.count && !returning; i++) {
      auto prev = env;
      env = repeatEnv;
      for (auto& s : rp.body) {
        execute(*s);
        if (returning || breaking || continuing) break;
      }
      env = prev;
      if (breaking) { breaking = false; break; }
      if (continuing) { continuing = false; }
    }
    break;
  }
  case StmtType::For: {
    auto& fr = static_cast<ForStmt&>(stmt);
    auto forEnv = acquireEnv(env);
    auto prev = env;
    env = forEnv;
    if (fr.initializer) execute(*fr.initializer);
    if (fr.body->stype == StmtType::Block) {
      auto& body = static_cast<BlockStmt&>(*fr.body);
      auto bodyEnv = acquireEnv(env);
      while (!returning) {
        if (fr.condition && !isTruthy(evaluate(fr.condition.get()))) break;
        env = bodyEnv;
        for (auto& s : body.statements) {
          execute(*s);
          if (returning || breaking || continuing) break;
        }
        if (breaking) { breaking = false; break; }
        if (continuing) {
          continuing = false;
          if (fr.increment) evaluate(fr.increment.get());
          continue;
        }
        if (fr.increment) evaluate(fr.increment.get());
      }
    } else {
      while (!returning) {
        if (fr.condition && !isTruthy(evaluate(fr.condition.get()))) break;
        execute(*fr.body);
        if (breaking) { breaking = false; break; }
        if (continuing) {
          continuing = false;
          if (fr.increment) evaluate(fr.increment.get());
          continue;
        }
        if (fr.increment) evaluate(fr.increment.get());
      }
    }
    env = prev;
    break;
  }
  case StmtType::ForEach: {
    auto& fe = static_cast<ForEachStmt&>(stmt);
    int feId = internCached(fe.variable, fe.id);
    auto iterable = evaluate(fe.iterable.get());
    if (iterable.isFunction()) throw PTRuntimeError("for-each requires an array or string");
    auto forEachEnv = acquireEnv(env);
    auto prev = env;
    env = forEachEnv;
    if (iterable.isArray()) {
      for (auto& elem : *iterable.array) {
        if (returning) break;
        env->set(feId, elem);
        execute(*fe.body);
        if (breaking) { breaking = false; break; }
        if (continuing) { continuing = false; }
      }
    } else {
      for (char c : iterable.value) {
        if (returning) break;
        env->set(feId, PTValue(std::string(1, c)));
        execute(*fe.body);
        if (breaking) { breaking = false; break; }
        if (continuing) { continuing = false; }
      }
    }
    env = prev;
    break;
  }
  case StmtType::Try: {
    auto& ts = static_cast<TryStmt&>(stmt);
    try {
      auto tryEnv = acquireEnv(env);
      executeBlock(ts.tryBody, tryEnv);
    } catch (const PTRuntimeError& err) {
      if (!ts.catchBody.empty()) {
        auto catchEnv = acquireEnv(env);
        if (!ts.catchVar.empty()) {
          int catchId = internCached(ts.catchVar, ts.catchId);
          catchEnv->set(catchId, PTValue(err.what()));
        }
        executeBlock(ts.catchBody, catchEnv);
      }
    }
    if (!ts.finallyBody.empty()) {
      auto finallyEnv = acquireEnv(env);
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
      for (auto& s : stmts) { execute(*s); if (returning) break; }
      env = prevEnv;
      auto modMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
      for (auto& [k, v] : moduleEnv->values) (*modMap)[interner.name(k)] = v;
      int aliasId = interner.intern(imp.alias);
      defineVar(aliasId, PTValue(modMap));
    } else {
      for (auto& s : stmts) { execute(*s); if (returning) break; }
    }
    break;
  }
  case StmtType::Class: {
    auto& cs = static_cast<ClassStmt&>(stmt);
    auto klass = std::make_shared<PTClass>();
    klass->name = cs.name;
    klass->parentName = cs.parent;
    if (!cs.parent.empty()) {
      int parentId = interner.intern(cs.parent);
      const PTValue& parentVal = getVar(parentId);
      if (!parentVal.isClass()) throw PTRuntimeError("'" + cs.parent + "' is not a class");
      klass->parent = parentVal.klass;
    }
    for (auto& sm : cs.staticMethods) {
      auto func = std::make_shared<PTFunction>();
      func->name = sm->name;
      func->params = sm->params;
      func->paramIds.resize(sm->params.size());
      for (size_t i = 0; i < sm->params.size(); i++)
        func->paramIds[i] = interner.intern(sm->params[i]);
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
      func->paramIds.resize(m->params.size());
      for (size_t i = 0; i < m->params.size(); i++)
        func->paramIds[i] = interner.intern(m->params[i]);
      func->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>(std::move(m->body));
      func->closure = env;
      if (m->name == "init") func->isInit = true;
      klass->methods[m->name] = PTValue(func);
    }
    klass->fields = std::move(cs.fields);
    env = prev;
    for (auto& [k, v] : klass->staticMethods) defineVar(interner.intern(k), v);
    defineVar(interner.intern(cs.name), PTValue(klass));
    break;
  }
  case StmtType::Enum: {
    auto& es = static_cast<EnumStmt&>(stmt);
    auto m = std::make_shared<std::unordered_map<std::string, PTValue>>();
    for (size_t i = 0; i < es.values.size(); i++)
      (*m)[es.values[i]] = PTValue(static_cast<double>(i));
    defineVar(interner.intern(es.name), PTValue(m));
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
  for (auto& stmt : stmts) {
    execute(*stmt);
    if (returning || breaking || continuing) break;
  }
  env = prev;
}

PTValue Interpreter::evaluate(Expr* expr) {
  switch (expr->type) {
  case ExprType::Literal: {
    auto* l = static_cast<Literal*>(expr);
    if (l->isNumber) return PTValue(std::stod(l->value));
    if (l->isBool) return l->value == "true" ? PT_TRUE : PT_FALSE;
    if (l->isNil) return PT_NIL;
    return PTValue(l->value);
  }
  case ExprType::Variable: {
    auto* v = static_cast<Variable*>(expr);
    int id = internCached(v->name, v->id);
    return PTValue(getVar(id));
  }
  case ExprType::Grouping:
    return evaluate(static_cast<Grouping*>(expr)->expression.get());
  case ExprType::ThisExpr: {
    static int thisId = -1;
    if (thisId < 0) thisId = interner.intern("this");
    return PTValue(getVar(thisId));
  }
  case ExprType::SuperExpr: {
    auto* se = static_cast<SuperExpr*>(expr);
    static int thisId = -1;
    if (thisId < 0) thisId = interner.intern("this");
    const PTValue& thisVal = getVar(thisId);
    if (!thisVal.isInstance()) throw PTRuntimeError("'super' can only be used in a method");
    auto instance = thisVal.instance;
    auto parent = instance->klass->parent;
    if (!parent) throw PTRuntimeError("No parent class to call super on");
    auto methodIt = parent->methods.find(se->method);
    if (methodIt == parent->methods.end()) throw PTRuntimeError("Undefined parent method '" + se->method + "'");
    auto method = methodIt->second.function;
    auto methodEnv = acquireEnv(method->closure);
    methodEnv->set(thisId, thisVal);
    auto prev = env;
    env = methodEnv;
    for (auto& s : *method->body) {
      execute(*s);
      if (returning) break;
    }
    PTValue result = std::move(returnValue);
    returning = false;
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
    PTValue right = evaluate(u->right.get());
    if (right.isFunction() || right.isArray()) throw PTRuntimeError("Cannot use unary on function or array");
    if (u->op == "-") {
      if (right.isNumber()) return PTValue(-right.numValue);
      return PTValue(-std::stod(right.value));
    }
    if (u->op == "!" || u->op == "not") return isTruthy(right) ? PT_FALSE : PT_TRUE;
    break;
  }
  case ExprType::PostfixExpr: {
    auto* pe = static_cast<PostfixExpr*>(expr);
    auto* operandVar = static_cast<Variable*>(pe->operand.get());
    int id = internCached(operandVar->name, operandVar->id);
    const PTValue& val = getVar(id);
    if (pe->op == "++") {
      if (val.isFunction() || val.isArray() || val.isMap()) throw PTRuntimeError("Cannot increment non-number");
      PTValue result(val.numValue + 1);
      assignVar(id, result);
      return result;
    }
    if (pe->op == "--") {
      if (val.isFunction() || val.isArray() || val.isMap()) throw PTRuntimeError("Cannot decrement non-number");
      PTValue result(val.numValue - 1);
      assignVar(id, result);
      return result;
    }
    break;
  }
  case ExprType::ListCompExpr: {
    auto* lc = static_cast<ListCompExpr*>(expr);
    int lcId = internCached(lc->variable, lc->id);
    auto iterable = evaluate(lc->iterable.get());
    if (!iterable.isArray()) throw PTRuntimeError("List comprehension requires an array");
    auto result = std::make_shared<std::vector<PTValue>>();
    auto loopEnv = acquireEnv(env);
    for (auto& elem : *iterable.array) {
      if (returning) break;
      env = loopEnv;
      env->set(lcId, elem);
      if (lc->condition) {
        if (isTruthy(evaluate(lc->condition.get())))
          result->push_back(evaluate(lc->element.get()));
      } else {
        result->push_back(evaluate(lc->element.get()));
      }
    }
    return PTValue(result);
  }
  case ExprType::Binary: {
    auto* b = static_cast<Binary*>(expr);
    PTValue left = evaluate(b->left.get());
    PTValue right = evaluate(b->right.get());

    if (b->op == "in") {
      if (right.isArray()) {
        for (auto& elem : *right.array)
          if (isEqual(elem, left)) return PT_TRUE;
        return PT_FALSE;
      }
      if (right.isMap()) return right.map->count(left.value) ? PT_TRUE : PT_FALSE;
      if (!right.isFunction())
        return left.value.size() > 0 && right.value.find(left.value) != std::string::npos ? PT_TRUE : PT_FALSE;
      throw PTRuntimeError("'in' requires array, map, or string on right side");
    }

    if (left.isFunction() || right.isFunction()) throw PTRuntimeError("Cannot use binary on function");

    if (b->op == "==" || b->op == "is") {
      if (left.isNumber() && right.isNumber()) return left.numValue == right.numValue ? PT_TRUE : PT_FALSE;
      return isEqual(left, right) ? PT_TRUE : PT_FALSE;
    }
    if (b->op == "!=" || b->op == "isnt") {
      if (left.isNumber() && right.isNumber()) return left.numValue != right.numValue ? PT_TRUE : PT_FALSE;
      return isEqual(left, right) ? PT_FALSE : PT_TRUE;
    }

    if (left.isNumber() && right.isNumber()) {
      double l = left.numValue, r = right.numValue;
      if (b->op == "+") return PTValue(l + r);
      if (b->op == "-") return PTValue(l - r);
      if (b->op == "*") return PTValue(l * r);
      if (b->op == "/") { if (r == 0) throw PTRuntimeError("Division by zero"); return PTValue(l / r); }
      if (b->op == "%") { if (r == 0) throw PTRuntimeError("Modulo by zero"); return PTValue(std::fmod(l, r)); }
      if (b->op == "<") return l < r ? PT_TRUE : PT_FALSE;
      if (b->op == "<=") return l <= r ? PT_TRUE : PT_FALSE;
      if (b->op == ">") return l > r ? PT_TRUE : PT_FALSE;
      if (b->op == ">=") return l >= r ? PT_TRUE : PT_FALSE;
    }

    if (b->op == "+") {
      if (left.isArray() || right.isArray()) throw PTRuntimeError("Cannot add arrays with +");
      return PTValue(left.ensureStr() + right.ensureStr());
    }

    if (b->op == "*") {
      if ((left.isArray() || right.isArray()) && !(left.isArray() && right.isArray()))
        throw PTRuntimeError("Cannot multiply arrays");
      if (!left.isArray() && !left.isMap() && !left.isFunction() && !right.isArray() && !right.isMap() && !right.isFunction()) {
        if (left.isNumber() && !right.isNumber()) {
          int count = (int)left.numValue;
          std::string s = right.ensureStr();
          std::string result;
          result.reserve(count * s.size());
          for (int i = 0; i < count; i++) result += s;
          return PTValue(result);
        }
        if (!left.isNumber() && right.isNumber()) {
          int count = (int)right.numValue;
          std::string s = left.ensureStr();
          std::string result;
          result.reserve(count * s.size());
          for (int i = 0; i < count; i++) result += s;
          return PTValue(result);
        }
        if (!left.isNumber() && !right.isNumber())
          return PTValue(std::stod(left.value) * std::stod(right.value));
      }
      return PTValue(left.numValue * right.numValue);
    }

    if (left.isArray() || left.isMap()) throw PTRuntimeError("Cannot use arithmetic on arrays or maps");
    if (right.isArray() || right.isMap()) throw PTRuntimeError("Cannot use arithmetic on arrays or maps");
    double l = toDouble(left), r = toDouble(right);
    if (b->op == "-") return PTValue(l - r);
    if (b->op == "/") { if (r == 0) throw PTRuntimeError("Division by zero"); return PTValue(l / r); }
    if (b->op == "%") { if (r == 0) throw PTRuntimeError("Modulo by zero"); return PTValue(std::fmod(l, r)); }
    if (b->op == "<") return l < r ? PT_TRUE : PT_FALSE;
    if (b->op == "<=") return l <= r ? PT_TRUE : PT_FALSE;
    if (b->op == ">") return l > r ? PT_TRUE : PT_FALSE;
    if (b->op == ">=") return l >= r ? PT_TRUE : PT_FALSE;
    break;
  }
  case ExprType::Logical: {
    auto* l = static_cast<Logical*>(expr);
    PTValue left = evaluate(l->left.get());
    if (l->op == "or") { if (isTruthy(left)) return left; }
    else { if (!isTruthy(left)) return left; }
    return evaluate(l->right.get());
  }
  case ExprType::Assign: {
    auto* a = static_cast<Assign*>(expr);
    int id = internCached(a->name, a->id);
    PTValue val = evaluate(a->value.get());
    assignVar(id, val);
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
      if (key.isArray() || key.isFunction() || key.isMap()) throw PTRuntimeError("Map key must be a string");
      (*m)[key.value] = evaluate(v.get());
    }
    return PTValue(m);
  }
  case ExprType::DotExpr: {
    auto* de = static_cast<DotExpr*>(expr);
    PTValue obj = evaluate(de->object.get());
    if (obj.isInstance()) {
      auto& inst = obj.instance;
      auto it = inst->fields.find(de->name);
      if (it != inst->fields.end()) return it->second;
      auto mit = inst->klass->methods.find(de->name);
      if (mit != inst->klass->methods.end()) {
        auto method = mit->second.function;
        auto mf = std::make_shared<PTFunction>();
        mf->name = method->name; mf->params = method->params; mf->paramIds = method->paramIds;
        mf->body = method->body; mf->closure = method->closure;
        static int thisId = -1;
        if (thisId < 0) thisId = interner.intern("this");
        auto me = std::make_shared<Environment>(mf->closure);
        me->set(thisId, obj);
        mf->closure = me;
        return PTValue(mf);
      }
      if (inst->klass->parent) {
        auto pit = inst->klass->parent->methods.find(de->name);
        if (pit != inst->klass->parent->methods.end()) {
          auto method = pit->second.function;
          auto mf = std::make_shared<PTFunction>();
          mf->name = method->name; mf->params = method->params; mf->paramIds = method->paramIds;
          mf->body = method->body; mf->closure = method->closure;
          static int thisId = -1;
          if (thisId < 0) thisId = interner.intern("this");
          auto me = std::make_shared<Environment>(mf->closure);
          me->set(thisId, obj);
          mf->closure = me;
          return PTValue(mf);
        }
      }
      throw PTRuntimeError("Undefined property '" + de->name + "'");
    }
    if (obj.isClass()) {
      auto sit = obj.klass->staticMethods.find(de->name);
      if (sit != obj.klass->staticMethods.end()) return sit->second;
      auto mit = obj.klass->methods.find(de->name);
      if (mit != obj.klass->methods.end()) return mit->second;
      throw PTRuntimeError("Undefined property '" + de->name + "'");
    }
    if (obj.isMap()) {
      auto it = obj.map->find(de->name);
      if (it == obj.map->end()) throw PTRuntimeError("Undefined property '" + de->name + "'");
      return it->second;
    }
    throw PTRuntimeError("Cannot access property on non-map, class, or instance");
  }
  case ExprType::DotAssignExpr: {
    auto* da = static_cast<DotAssignExpr*>(expr);
    PTValue obj = evaluate(da->object.get());
    if (obj.isInstance()) {
      PTValue val = evaluate(da->value.get());
      obj.instance->fields[da->name] = val;
      return val;
    }
    if (!obj.isMap()) throw PTRuntimeError("Cannot assign property on non-map");
    PTValue val = evaluate(da->value.get());
    (*obj.map)[da->name] = val;
    return val;
  }
  case ExprType::IndexExpr: {
    auto* ix = static_cast<IndexExpr*>(expr);
    PTValue callee = evaluate(ix->callee.get());
    PTValue idx = evaluate(ix->index.get());
    if (callee.isMap()) {
      if (idx.isArray() || idx.isFunction() || idx.isMap()) throw PTRuntimeError("Map key must be a string");
      auto it = callee.map->find(idx.value);
      if (it == callee.map->end()) throw PTRuntimeError("Undefined key '" + idx.value + "'");
      return it->second;
    }
    if (idx.isArray() || idx.isFunction()) throw PTRuntimeError("Index must be a number");
    int i = idx.isNumber() ? (int)idx.numValue : (int)std::stod(idx.value);
    if (callee.isArray()) {
      int size = (int)callee.array->size();
      if (i < 0) i += size;
      if (i < 0 || i >= size) throw PTRuntimeError("Index out of bounds");
      return (*callee.array)[i];
    }
    if (!callee.isFunction()) {
      int size = (int)callee.value.size();
      if (i < 0) i += size;
      if (i < 0 || i >= size) throw PTRuntimeError("String index out of bounds");
      return PTValue(std::string(1, callee.value[i]));
    }
    throw PTRuntimeError("Cannot index into this value");
  }
  case ExprType::AssignIndex: {
    auto* ai = static_cast<AssignIndex*>(expr);
    PTValue callee = evaluate(ai->callee.get());
    if (!callee.isArray()) throw PTRuntimeError("Can only assign index into arrays");
    PTValue idx = evaluate(ai->index.get());
    if (idx.isArray() || idx.isFunction()) throw PTRuntimeError("Array index must be a number");
    int i = idx.isNumber() ? (int)idx.numValue : (int)std::stod(idx.value);
    int size = (int)callee.array->size();
    if (i < 0) i += size;
    if (i < 0 || i >= size) throw PTRuntimeError("Index out of bounds");
    PTValue val = evaluate(ai->value.get());
    (*callee.array)[i] = val;
    return val;
  }
  case ExprType::Call: {
    auto* c = static_cast<Call*>(expr);
    if (c->callee->type == ExprType::Variable) {
      auto* var = static_cast<Variable*>(c->callee.get());
      int varId = internCached(var->name, var->id);
      const PTValue* direct = findVar(varId);
      if (!direct) {
        PTValue result = callBuiltin(var->name, c->arguments);
        if (!result.isFunction()) return result;
        throw PTRuntimeError("Undefined variable '" + var->name + "'");
      }
      PTValue callee(*direct);
      if (callee.isClass()) {
        auto instance = std::make_shared<PTInstance>();
        instance->klass = callee.klass;
        auto instanceVal = PTValue(instance);
        std::vector<PTClass*> chain;
        for (auto* k = callee.klass.get(); k; k = k->parent.get()) chain.push_back(k);
        for (int ci = (int)chain.size() - 1; ci >= 0; ci--)
          for (auto& [fname, fexpr] : chain[ci]->fields)
            instance->fields[fname] = fexpr ? evaluate(fexpr.get()) : PTValue();
        PTFunction* initFunc = nullptr;
        for (auto* k : chain) {
          auto it = k->methods.find("init");
          if (it != k->methods.end()) { initFunc = it->second.function.get(); break; }
        }
        if (initFunc) {
          static int thisId = -1;
          if (thisId < 0) thisId = interner.intern("this");
          auto callEnv = acquireEnv(initFunc->closure);
          callEnv->set(thisId, instanceVal);
          auto prev = env;
          env = callEnv;
          for (size_t i = 0; i < initFunc->params.size(); i++) {
            PTValue argVal;
            if (i < c->arguments.size()) argVal = evaluate(c->arguments[i].get());
            callEnv->set(initFunc->paramIds[i], std::move(argVal));
          }
          for (auto& s : *initFunc->body) {
            execute(*s);
            if (returning) break;
          }
          returning = false;
          env = prev;
        }
        return instanceVal;
      }
      if (!callee.isFunction()) throw PTRuntimeError("Can only call functions");
      auto& fn = callee.function;
      size_t argCount = c->arguments.size();
      size_t paramCount = fn->params.size();
      if (argCount != paramCount)
        throw PTRuntimeError("Expected " + std::to_string(paramCount) + " arguments but got " + std::to_string(argCount));
      auto callEnv = acquireEnv(fn->closure);
      for (size_t i = 0; i < fn->params.size(); i++)
        callEnv->setNew(fn->paramIds[i], evaluate(c->arguments[i].get()));
      auto prev = env;
      env = callEnv;
      for (auto& stmt : *fn->body) {
        execute(*stmt);
        if (returning) break;
      }
      PTValue result = std::move(returnValue);
      returning = false;
      returnValue = PTValue();
      env = prev;
      return result;
    }
    PTValue callee = evaluate(c->callee.get());
    if (callee.isClass()) {
      auto instance = std::make_shared<PTInstance>();
      instance->klass = callee.klass;
      auto instanceVal = PTValue(instance);
      std::vector<PTClass*> chain;
      for (auto* k = callee.klass.get(); k; k = k->parent.get()) chain.push_back(k);
      for (int ci = (int)chain.size() - 1; ci >= 0; ci--)
        for (auto& [fname, fexpr] : chain[ci]->fields)
          instance->fields[fname] = fexpr ? evaluate(fexpr.get()) : PTValue();
      PTFunction* initFunc = nullptr;
      for (auto* k : chain) {
        auto it = k->methods.find("init");
        if (it != k->methods.end()) { initFunc = it->second.function.get(); break; }
      }
      if (initFunc) {
        static int thisId = -1;
        if (thisId < 0) thisId = interner.intern("this");
        auto callEnv = acquireEnv(initFunc->closure);
        callEnv->setNew(thisId, instanceVal);
        auto prev = env;
        env = callEnv;
        for (size_t i = 0; i < initFunc->params.size(); i++) {
          PTValue argVal;
          if (i < c->arguments.size()) argVal = evaluate(c->arguments[i].get());
          callEnv->setNew(initFunc->paramIds[i], std::move(argVal));
        }
        for (auto& s : *initFunc->body) {
          execute(*s);
          if (returning) break;
        }
        returning = false;
        env = prev;
      }
      return instanceVal;
    }
    if (!callee.isFunction()) throw PTRuntimeError("Can only call functions");
    auto& fn = callee.function;
    size_t argCount = c->arguments.size();
    size_t paramCount = fn->params.size();
    if (argCount != paramCount)
      throw PTRuntimeError("Expected " + std::to_string(paramCount) + " arguments but got " + std::to_string(argCount));
    auto callEnv = acquireEnv(fn->closure);
    for (size_t i = 0; i < fn->params.size(); i++)
      callEnv->setNew(fn->paramIds[i], evaluate(c->arguments[i].get()));
    auto prev = env;
    env = callEnv;
    for (auto& stmt : *fn->body) {
      execute(*stmt);
      if (returning) break;
    }
    PTValue result = std::move(returnValue);
    returning = false;
    returnValue = PTValue();
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
    func->paramIds.resize(le->params.size());
    for (size_t i = 0; i < le->params.size(); i++)
      func->paramIds[i] = interner.intern(le->params[i]);
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
    PTValue value = evaluate(me->value.get());
    for (auto& mc : me->cases) {
      for (auto& pattern : mc->patterns) {
        if (pattern->type == ExprType::Variable) {
          if (static_cast<Variable*>(pattern.get())->name == "_") {
            auto matchEnv = acquireEnv(env);
            auto prev = env;
            env = matchEnv;
            auto result = evaluate(mc->body.get());
            env = prev;
            return result;
          }
        }
        PTValue patVal = evaluate(pattern.get());
        if (isEqual(value, patVal)) {
          auto matchEnv = acquireEnv(env);
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
  case ExprType::MatchCase:
    throw PTRuntimeError("Match case should not be evaluated directly");
  }
  throw PTRuntimeError("Unknown expression");
}

PTValue Interpreter::evaluateFunction(const PTValue& fnVal, const std::vector<PTValue>& args) {
  if (!fnVal.isFunction()) throw PTRuntimeError("Can only call functions");
  auto& fn = fnVal.function;
  if (args.size() != fn->params.size())
    throw PTRuntimeError("Expected " + std::to_string(fn->params.size()) + " arguments but got " + std::to_string(args.size()));
  auto funcEnv = acquireEnv(fn->closure);
  for (size_t i = 0; i < fn->params.size(); i++)
    funcEnv->setNew(fn->paramIds[i], args[i]);
  auto prev = env;
  env = funcEnv;
  for (auto& stmt : *fn->body) {
    execute(*stmt);
    if (returning) break;
  }
  PTValue result = std::move(returnValue);
  returning = false;
  returnValue = PTValue();
  env = prev;
  return result;
}

bool Interpreter::isTruthy(const PTValue& val) {
  switch (val.type) {
  case PTValue::TBool: return val.boolValue;
  case PTValue::TNumber: return val.numValue != 0;
  case PTValue::TNil: return false;
  case PTValue::TString: return !val.value.empty() && val.value != "false" && val.value != "nil" && val.value != "0";
  case PTValue::TFunction: return true;
  case PTValue::TArray: return val.array->size() > 0;
  case PTValue::TMap: return val.map->size() > 0;
  case PTValue::TClass: return true;
  case PTValue::TInstance: return true;
  case PTValue::TDatabase: return true;
  }
  return false;
}

bool Interpreter::isEqual(const PTValue& a, const PTValue& b) {
  if (a.type != b.type) {
    if (a.isNumber() && b.isNumber()) return a.numValue == b.numValue;
    if ((a.isNumber() || b.isNumber()) && (a.isString() || b.isString()))
      return a.ensureStr() == b.ensureStr();
    return false;
  }
  switch (a.type) {
  case PTValue::TNumber: return a.numValue == b.numValue;
  case PTValue::TBool: return a.boolValue == b.boolValue;
  case PTValue::TNil: return true;
  case PTValue::TString: return a.value == b.value;
  case PTValue::TFunction: return false;
  case PTValue::TDatabase: return a.db == b.db;
  case PTValue::TArray: {
    if (a.array->size() != b.array->size()) return false;
    for (size_t i = 0; i < a.array->size(); i++)
      if (!isEqual((*a.array)[i], (*b.array)[i])) return false;
    return true;
  }
  case PTValue::TMap: {
    if (a.map->size() != b.map->size()) return false;
    for (auto& [k, v] : *a.map) {
      if (!b.map->count(k)) return false;
      if (!isEqual(v, (*b.map)[k])) return false;
    }
    return true;
  }
  case PTValue::TClass: return a.klass == b.klass;
  case PTValue::TInstance: return a.instance == b.instance;
  }
  return false;
}

PTValue Interpreter::callBuiltin(const std::string& name, const std::vector<std::unique_ptr<Expr>>& args) {
  if (name == "len") {
    if (args.size() != 1) throw PTRuntimeError("len() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isFunction()) throw PTRuntimeError("len() expects a string or array");
    if (arg.isArray()) return PTValue(static_cast<double>(arg.array->size()));
    return PTValue(static_cast<double>(arg.value.size()));
  }
  if (name == "push") {
    if (args.size() != 2) throw PTRuntimeError("push() expects 2 arguments");
    auto arr = evaluate(args[0].get());
    if (!arr.isArray()) throw PTRuntimeError("push() expects an array");
    arr.array->push_back(evaluate(args[1].get()));
    return PT_TRUE;
  }
  if (name == "pop") {
    if (args.size() != 1) throw PTRuntimeError("pop() expects 1 argument");
    auto arr = evaluate(args[0].get());
    if (!arr.isArray()) throw PTRuntimeError("pop() expects an array");
    if (arr.array->empty()) return PT_NIL;
    auto val = arr.array->back();
    arr.array->pop_back();
    return val;
  }
  if (name == "toNum") {
    if (args.size() != 1) throw PTRuntimeError("toNum() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isFunction() || arg.isArray()) throw PTRuntimeError("toNum() expects a string");
    try { return PTValue(std::stod(arg.value)); }
    catch (...) { return PT_NIL; }
  }
  if (name == "toString") {
    if (args.size() != 1) throw PTRuntimeError("toString() expects 1 argument");
    return PTValue(formatValue(evaluate(args[0].get())));
  }
  if (name == "input") {
    if (args.size() > 0) { auto prompt = evaluate(args[0].get()); std::cout << prompt.value; }
    std::string line;
    if (!std::getline(std::cin, line)) return PT_NIL;
    return PTValue(line);
  }
  if (name == "readFile") {
    if (args.size() != 1) throw PTRuntimeError("readFile() expects 1 argument");
    auto path = evaluate(args[0].get());
    std::ifstream file(path.value);
    if (!file.is_open()) return PT_NIL;
    std::stringstream buf; buf << file.rdbuf(); return PTValue(buf.str());
  }
  if (name == "writeFile") {
    if (args.size() != 2) throw PTRuntimeError("writeFile() expects 2 arguments");
    auto path = evaluate(args[0].get());
    auto content = evaluate(args[1].get());
    std::ofstream file(path.value);
    if (!file.is_open()) return PT_FALSE;
    file << content.value; return PT_TRUE;
  }
  if (name == "abs") {
    if (args.size() != 1) throw PTRuntimeError("abs() expects 1 argument");
    return PTValue(std::fabs(toDouble(evaluate(args[0].get()))));
  }
  if (name == "sqrt") {
    if (args.size() != 1) throw PTRuntimeError("sqrt() expects 1 argument");
    return PTValue(std::sqrt(toDouble(evaluate(args[0].get()))));
  }
  if (name == "min") {
    if (args.size() != 2) throw PTRuntimeError("min() expects 2 arguments");
    double da = toDouble(evaluate(args[0].get())), db = toDouble(evaluate(args[1].get()));
    return PTValue(da < db ? da : db);
  }
  if (name == "max") {
    if (args.size() != 2) throw PTRuntimeError("max() expects 2 arguments");
    double da = toDouble(evaluate(args[0].get())), db = toDouble(evaluate(args[1].get()));
    return PTValue(da > db ? da : db);
  }
  if (name == "floor") {
    if (args.size() != 1) throw PTRuntimeError("floor() expects 1 argument");
    return PTValue(std::floor(toDouble(evaluate(args[0].get()))));
  }
  if (name == "ceil") {
    if (args.size() != 1) throw PTRuntimeError("ceil() expects 1 argument");
    return PTValue(std::ceil(toDouble(evaluate(args[0].get()))));
  }
  if (name == "round") {
    if (args.size() != 1) throw PTRuntimeError("round() expects 1 argument");
    return PTValue(std::round(toDouble(evaluate(args[0].get()))));
  }
  if (name == "type") {
    if (args.size() != 1) throw PTRuntimeError("type() expects 1 argument");
    auto arg = evaluate(args[0].get());
    switch (arg.type) {
    case PTValue::TFunction: return PTValue("function");
    case PTValue::TArray: return PTValue("array");
    case PTValue::TMap: return PTValue("map");
    case PTValue::TClass: return PTValue("class");
    case PTValue::TInstance: return PTValue("instance");
    case PTValue::TNil: return PTValue("nil");
    case PTValue::TNumber: return PTValue("number");
    case PTValue::TDatabase: return PTValue("database");
    case PTValue::TBool: return PTValue("bool");
    case PTValue::TString: return PTValue("string");
    }
    return PTValue("string");
  }
  if (name == "keys") {
    if (args.size() != 1) throw PTRuntimeError("keys() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (!arg.isMap()) throw PTRuntimeError("keys() expects a map");
    auto arr = std::make_shared<std::vector<PTValue>>();
    for (auto& [k, v] : *arg.map) arr->push_back(PTValue(k));
    return PTValue(arr);
  }
  if (name == "values") {
    if (args.size() != 1) throw PTRuntimeError("values() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (!arg.isMap()) throw PTRuntimeError("values() expects a map");
    auto arr = std::make_shared<std::vector<PTValue>>();
    for (auto& [k, v] : *arg.map) arr->push_back(v);
    return PTValue(arr);
  }
  if (name == "has") {
    if (args.size() != 2) throw PTRuntimeError("has() expects 2 arguments");
    auto obj = evaluate(args[0].get());
    auto key = evaluate(args[1].get());
    if (!obj.isMap()) throw PTRuntimeError("has() expects a map");
    if (key.isArray() || key.isFunction() || key.isMap()) throw PTRuntimeError("has() key must be a string");
    return obj.map->count(key.value) ? PT_TRUE : PT_FALSE;
  }
  if (name == "upper") {
    if (args.size() != 1) throw PTRuntimeError("upper() expects 1 argument");
    auto arg = evaluate(args[0].get());
    std::string s = arg.value;
    for (auto& c : s) c = std::toupper(c);
    return PTValue(s);
  }
  if (name == "lower") {
    if (args.size() != 1) throw PTRuntimeError("lower() expects 1 argument");
    auto arg = evaluate(args[0].get());
    std::string s = arg.value;
    for (auto& c : s) c = std::tolower(c);
    return PTValue(s);
  }
  if (name == "trim") {
    if (args.size() != 1) throw PTRuntimeError("trim() expects 1 argument");
    std::string s = evaluate(args[0].get()).value;
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return PTValue("");
    size_t end = s.find_last_not_of(" \t\n\r");
    return PTValue(s.substr(start, end - start + 1));
  }
  if (name == "substr") {
    if (args.size() < 2 || args.size() > 3) throw PTRuntimeError("substr() expects 2 or 3 arguments");
    auto str = evaluate(args[0].get());
    auto startArg = evaluate(args[1].get());
    int start = startArg.isNumber() ? (int)startArg.numValue : (int)std::stod(startArg.value);
    int len = (int)str.value.size() - start;
    if (args.size() == 3) {
      auto lenArg = evaluate(args[2].get());
      len = lenArg.isNumber() ? (int)lenArg.numValue : (int)std::stod(lenArg.value);
    }
    if (start < 0 || start >= (int)str.value.size()) throw PTRuntimeError("substr() start out of bounds");
    if (start + len > (int)str.value.size()) len = (int)str.value.size() - start;
    return PTValue(str.value.substr(start, len));
  }
  if (name == "contains") {
    if (args.size() != 2) throw PTRuntimeError("contains() expects 2 arguments");
    auto str = evaluate(args[0].get());
    auto sub = evaluate(args[1].get());
    return str.value.find(sub.value) != std::string::npos ? PT_TRUE : PT_FALSE;
  }
  if (name == "replace") {
    if (args.size() != 3) throw PTRuntimeError("replace() expects 3 arguments");
    auto str = evaluate(args[0].get());
    auto old = evaluate(args[1].get());
    auto newStr = evaluate(args[2].get());
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
    auto arr = std::make_shared<std::vector<PTValue>>();
    std::string s = str.value;
    std::string d = delim.value;
    if (d.empty()) {
      for (char c : s) arr->push_back(PTValue(std::string(1, c)));
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
    return PT_TRUE;
  }
  if (name == "join") {
    if (args.size() != 2) throw PTRuntimeError("join() expects 2 arguments");
    auto arr = evaluate(args[0].get());
    auto delim = evaluate(args[1].get());
    if (!arr.isArray()) throw PTRuntimeError("join() expects an array");
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
    if (haystack.isArray()) {
      for (size_t i = 0; i < haystack.array->size(); i++)
        if (isEqual((*haystack.array)[i], needle))
          return PTValue(static_cast<double>(i));
      return PTValue(-1.0);
    }
    if (haystack.isFunction()) throw PTRuntimeError("indexOf() expects a string or array");
    auto pos = haystack.value.find(needle.value);
    if (pos == std::string::npos) return PTValue(-1.0);
    return PTValue(static_cast<double>(pos));
  }
  if (name == "sort") {
    if (args.size() != 1) throw PTRuntimeError("sort() expects 1 argument");
    auto arr = evaluate(args[0].get());
    if (!arr.isArray()) throw PTRuntimeError("sort() expects an array");
    auto sorted = std::make_shared<std::vector<PTValue>>(*arr.array);
    std::sort(sorted->begin(), sorted->end(), [](const PTValue& a, const PTValue& b) {
      if (a.isNumber() && b.isNumber()) return a.numValue < b.numValue;
      return a.ensureStr() < b.ensureStr();
    });
    return PTValue(sorted);
  }
  if (name == "range") {
    if (args.size() < 1 || args.size() > 2) throw PTRuntimeError("range() expects 1 or 2 arguments");
    auto arr = std::make_shared<std::vector<PTValue>>();
    if (args.size() == 1) {
      int end = (int)toDouble(evaluate(args[0].get()));
      arr->reserve(end);
      for (int i = 0; i < end; i++) arr->push_back(PTValue(static_cast<double>(i)));
    } else {
      int start = (int)toDouble(evaluate(args[0].get()));
      int end = (int)toDouble(evaluate(args[1].get()));
      if (start <= end) { arr->reserve(end - start); for (int i = start; i < end; i++) arr->push_back(PTValue(static_cast<double>(i))); }
      else { arr->reserve(start - end); for (int i = start; i > end; i--) arr->push_back(PTValue(static_cast<double>(i))); }
    }
    return PTValue(arr);
  }
  if (name == "map") {
    if (args.size() != 2) throw PTRuntimeError("map() expects 2 arguments");
    auto arr = evaluate(args[0].get());
    auto fn = evaluate(args[1].get());
    if (!arr.isArray()) throw PTRuntimeError("map() expects an array");
    if (!fn.isFunction()) throw PTRuntimeError("map() expects a function");
    auto result = std::make_shared<std::vector<PTValue>>();
    for (auto& elem : *arr.array) result->push_back(evaluateFunction(fn, {elem}));
    return PTValue(result);
  }
  if (name == "filter") {
    if (args.size() != 2) throw PTRuntimeError("filter() expects 2 arguments");
    auto arr = evaluate(args[0].get());
    auto fn = evaluate(args[1].get());
    if (!arr.isArray()) throw PTRuntimeError("filter() expects an array");
    if (!fn.isFunction()) throw PTRuntimeError("filter() expects a function");
    auto result = std::make_shared<std::vector<PTValue>>();
    for (auto& elem : *arr.array)
      if (isTruthy(evaluateFunction(fn, {elem}))) result->push_back(elem);
    return PTValue(result);
  }
  if (name == "reduce") {
    if (args.size() < 2 || args.size() > 3) throw PTRuntimeError("reduce() expects 2 or 3 arguments");
    auto arr = evaluate(args[0].get());
    auto fn = evaluate(args[1].get());
    if (!arr.isArray()) throw PTRuntimeError("reduce() expects an array");
    if (!fn.isFunction()) throw PTRuntimeError("reduce() expects a function");
    PTValue acc;
    size_t start = 0;
    if (args.size() == 3) acc = evaluate(args[2].get());
    else {
      if (arr.array->empty()) throw PTRuntimeError("reduce() requires initial value for empty array");
      acc = (*arr.array)[0]; start = 1;
    }
    for (size_t i = start; i < arr.array->size(); i++)
      acc = evaluateFunction(fn, {acc, (*arr.array)[i]});
    return acc;
  }
  if (name == "random") {
    if (args.size() > 2) throw PTRuntimeError("random() expects 0, 1, or 2 arguments");
    static std::mt19937 rng(std::random_device{}());
    if (args.size() == 0) return PTValue(std::uniform_real_distribution<double>(0.0, 1.0)(rng));
    if (args.size() == 1) {
      int max = (int)toDouble(evaluate(args[0].get()));
      return PTValue(static_cast<double>(std::uniform_int_distribution<int>(0, max - 1)(rng)));
    }
    int min = (int)toDouble(evaluate(args[0].get()));
    int max = (int)toDouble(evaluate(args[1].get()));
    return PTValue(static_cast<double>(std::uniform_int_distribution<int>(min, max - 1)(rng)));
  }
  if (name == "clock") {
    if (args.size() != 0) throw PTRuntimeError("clock() expects no arguments");
    return PTValue(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  if (name == "getenv") {
    if (args.size() != 1) throw PTRuntimeError("getenv() expects 1 argument");
    auto key = evaluate(args[0].get());
    const char* val = std::getenv(key.value.c_str());
    return PTValue(val ? std::string(val) : "nil");
  }
  if (name == "fileExists") {
    if (args.size() != 1) throw PTRuntimeError("fileExists() expects 1 argument");
    std::ifstream f(evaluate(args[0].get()).value);
    return f.good() ? PT_TRUE : PT_FALSE;
  }
  if (name == "sqliteOpen") {
    if (args.size() != 1) throw PTRuntimeError("sqliteOpen() expects 1 argument");
    auto path = evaluate(args[0].get());
    sqlite3* db;
    if (sqlite3_open(path.value.c_str(), &db) != SQLITE_OK) {
      std::string err = sqlite3_errmsg(db); sqlite3_close(db);
      throw PTRuntimeError("Cannot open database: " + err);
    }
    return PTValue(db);
  }
  if (name == "sqliteExec") {
    if (args.size() != 2) throw PTRuntimeError("sqliteExec(db, sql) expects 2 arguments");
    auto dbArg = evaluate(args[0].get());
    auto sql = evaluate(args[1].get());
    if (!dbArg.isDatabase()) throw PTRuntimeError("sqliteExec() expects a database");
    char* errMsg = nullptr;
    if (sqlite3_exec(dbArg.db, sql.value.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
      std::string err = errMsg; sqlite3_free(errMsg);
      throw PTRuntimeError("SQL error: " + err);
    }
    return PT_TRUE;
  }
  if (name == "sqliteQuery") {
    if (args.size() != 2) throw PTRuntimeError("sqliteQuery(db, sql) expects 2 arguments");
    auto dbArg = evaluate(args[0].get());
    auto sql = evaluate(args[1].get());
    if (!dbArg.isDatabase()) throw PTRuntimeError("sqliteQuery() expects a database");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(dbArg.db, sql.value.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
      throw PTRuntimeError("SQL error: " + std::string(sqlite3_errmsg(dbArg.db)));
    auto rows = std::make_shared<std::vector<PTValue>>();
    int colCount = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      auto row = std::make_shared<std::unordered_map<std::string, PTValue>>();
      for (int i = 0; i < colCount; i++) {
        std::string colName = sqlite3_column_name(stmt, i);
        const unsigned char* text = sqlite3_column_text(stmt, i);
        if (text) (*row)[colName] = PTValue(std::string(reinterpret_cast<const char*>(text)));
        else (*row)[colName] = PT_NIL;
      }
      rows->push_back(PTValue(row));
    }
    sqlite3_finalize(stmt);
    return PTValue(rows);
  }
  if (name == "sqliteClose") {
    if (args.size() != 1) throw PTRuntimeError("sqliteClose() expects 1 argument");
    auto dbArg = evaluate(args[0].get());
    if (!dbArg.isDatabase()) throw PTRuntimeError("sqliteClose() expects a database");
    sqlite3_close(dbArg.db);
    return PT_TRUE;
  }
  if (name == "httpListen") {
    if (args.size() != 2) throw PTRuntimeError("httpListen(port, handler) expects 2 arguments");
    auto portVal = evaluate(args[0].get());
    int port = portVal.isNumber() ? (int)portVal.numValue : (int)std::stod(portVal.value);
    auto handler = evaluate(args[1].get());
    if (!handler.isFunction()) throw PTRuntimeError("httpListen() second argument must be a function");
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
      std::cout << req.method << " " << req.path << std::endl;
      auto savedEnv = env;
      static int reqId = -1;
      if (reqId < 0) reqId = interner.intern("req");
      auto handlerEnv = std::make_shared<Environment>(handler.function->closure);
      handlerEnv->set(reqId, PTValue(reqMap));
      env = handlerEnv;
      PTValue response;
      for (auto& s : *handler.function->body) {
        execute(*s);
        if (returning) break;
      }
      response = std::move(returnValue);
      returning = false;
      returnValue = PTValue();
      env = savedEnv;
      int status = 200;
      std::string statusText = "OK";
      std::string contentType = "text/html";
      std::string body;
      if (response.isMap()) {
        if (response.map->count("status")) {
          status = (int)toDouble((*response.map)["status"]);
          if (status == 404) statusText = "Not Found";
          else if (status == 500) statusText = "Internal Server Error";
          else if (status == 301) statusText = "Moved Permanently";
          else if (status == 201) statusText = "Created";
        }
        if (response.map->count("headers")) {
          auto& respHeaders = (*response.map)["headers"];
          if (respHeaders.isMap() && respHeaders.map->count("content-type"))
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
    return PT_NIL;
  }
  if (name == "parseJSON") {
    if (args.size() != 1) throw PTRuntimeError("parseJSON() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (!arg.isString()) throw PTRuntimeError("parseJSON() expects a string");
    return jsonParse(arg.value);
  }
  if (name == "toJSON") {
    if (args.size() < 1 || args.size() > 2) throw PTRuntimeError("toJSON() expects 1 or 2 arguments");
    auto val = evaluate(args[0].get());
    bool pretty = false;
    if (args.size() == 2) pretty = isTruthy(evaluate(args[1].get()));
    if (!pretty) return PTValue(jsonSerialize(val));
    std::string json = jsonSerialize(val);
    std::string out;
    int indent = 0;
    bool inStr = false;
    for (size_t i = 0; i < json.size(); i++) {
      char c = json[i];
      if (c == '"' && (i == 0 || json[i-1] != '\\')) { inStr = !inStr; out += c; continue; }
      if (inStr) { out += c; continue; }
      if (c == '{' || c == '[') { out += c; out += '\n'; indent++; out += std::string(indent * 2, ' '); }
      else if (c == '}' || c == ']') { out += '\n'; indent--; out += std::string(indent * 2, ' '); out += c; }
      else if (c == ',') { out += c; out += '\n'; out += std::string(indent * 2, ' '); }
      else if (c == ':') { out += ": "; }
      else { out += c; }
    }
    return PTValue(out);
  }
  if (name == "httpGet") {
    if (args.size() != 1) throw PTRuntimeError("httpGet() expects 1 argument");
    auto url = evaluate(args[0].get());
    auto resp = httpGet(url.value);
    auto respMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
    (*respMap)["status"] = PTValue(static_cast<double>(resp.status));
    (*respMap)["body"] = PTValue(resp.body);
    auto hdrMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
    for (auto& [k, v] : resp.headers) (*hdrMap)[k] = PTValue(v);
    (*respMap)["headers"] = PTValue(hdrMap);
    return PTValue(respMap);
  }
  if (name == "httpPost") {
    if (args.size() < 2 || args.size() > 3) throw PTRuntimeError("httpPost(url, body, [contentType]) expects 2 or 3 arguments");
    auto url = evaluate(args[0].get());
    auto body = evaluate(args[1].get());
    std::string ct = "application/json";
    if (args.size() == 3) ct = evaluate(args[2].get()).value;
    auto resp = httpPost(url.value, body.value, ct);
    auto respMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
    (*respMap)["status"] = PTValue(static_cast<double>(resp.status));
    (*respMap)["body"] = PTValue(resp.body);
    auto hdrMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
    for (auto& [k, v] : resp.headers) (*hdrMap)[k] = PTValue(v);
    (*respMap)["headers"] = PTValue(hdrMap);
    return PTValue(respMap);
  }
  if (name == "httpPut") {
    if (args.size() < 2 || args.size() > 3) throw PTRuntimeError("httpPut(url, body, [contentType]) expects 2 or 3 arguments");
    auto url = evaluate(args[0].get());
    auto body = evaluate(args[1].get());
    std::string ct = "application/json";
    if (args.size() == 3) ct = evaluate(args[2].get()).value;
    auto resp = httpPut(url.value, body.value, ct);
    auto respMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
    (*respMap)["status"] = PTValue(static_cast<double>(resp.status));
    (*respMap)["body"] = PTValue(resp.body);
    auto hdrMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
    for (auto& [k, v] : resp.headers) (*hdrMap)[k] = PTValue(v);
    (*respMap)["headers"] = PTValue(hdrMap);
    return PTValue(respMap);
  }
  if (name == "httpDelete") {
    if (args.size() != 1) throw PTRuntimeError("httpDelete() expects 1 argument");
    auto url = evaluate(args[0].get());
    auto resp = httpDelete(url.value);
    auto respMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
    (*respMap)["status"] = PTValue(static_cast<double>(resp.status));
    (*respMap)["body"] = PTValue(resp.body);
    auto hdrMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
    for (auto& [k, v] : resp.headers) (*hdrMap)[k] = PTValue(v);
    (*respMap)["headers"] = PTValue(hdrMap);
    return PTValue(respMap);
  }
  if (name == "hash") {
    if (args.size() < 1 || args.size() > 2) throw PTRuntimeError("hash() expects 1 or 2 arguments");
    auto input = evaluate(args[0].get());
    std::string algo = "sha256";
    if (args.size() == 2) {
      auto a = evaluate(args[1].get());
      algo = a.value;
    }
    if (algo == "sha256") return PTValue(cryptoSha256(input.value));
    if (algo == "md5") return PTValue(cryptoMd5(input.value));
    throw PTRuntimeError("hash() supports 'sha256' and 'md5'");
  }
  if (name == "base64Encode") {
    if (args.size() != 1) throw PTRuntimeError("base64Encode() expects 1 argument");
    auto arg = evaluate(args[0].get());
    return PTValue(cryptoBase64Encode(arg.value));
  }
  if (name == "base64Decode") {
    if (args.size() != 1) throw PTRuntimeError("base64Decode() expects 1 argument");
    auto arg = evaluate(args[0].get());
    return PTValue(cryptoBase64Decode(arg.value));
  }
  if (name == "uuid") {
    if (args.size() != 0) throw PTRuntimeError("uuid() expects no arguments");
    return PTValue(cryptoUuid());
  }
  if (name == "sleep") {
    if (args.size() != 1) throw PTRuntimeError("sleep() expects 1 argument");
    auto ms = evaluate(args[0].get());
    double millis = ms.isNumber() ? ms.numValue : std::stod(ms.value);
    std::this_thread::sleep_for(std::chrono::milliseconds((int)millis));
    return PT_NIL;
  }
  if (name == "spawn") {
    if (args.size() < 1) throw PTRuntimeError("spawn() expects at least 1 argument");
    auto fn = evaluate(args[0].get());
    if (!fn.isFunction()) throw PTRuntimeError("spawn() expects a function");
    std::vector<PTValue> spawnArgs;
    for (size_t i = 1; i < args.size(); i++) spawnArgs.push_back(evaluate(args[i].get()));
    auto capturedFn = fn.function;
    auto capturedArgs = std::make_shared<std::vector<PTValue>>(std::move(spawnArgs));
    std::thread t([capturedFn, capturedArgs]() {
      std::vector<PTValue> argsCopy = *capturedArgs;
      Interpreter interp;
      interp.globals = std::make_shared<Environment>();
      interp.env = interp.globals;
      interp.evaluateFunction(PTValue(capturedFn), argsCopy);
    });
    t.detach();
    return PT_TRUE;
  }
  if (name == "pgOpen") {
    if (args.size() != 1) throw PTRuntimeError("pgOpen() expects 1 argument");
#ifdef HAS_PG
    auto connStr = evaluate(args[0].get());
    void* conn = pgOpen(connStr.value);
    PTValue result;
    result.type = PTValue::TDatabase;
    result.db = (sqlite3*)conn;
    return result;
#else
    throw PTRuntimeError("PostgreSQL support not compiled. Build with 'make pg'");
#endif
  }
  if (name == "pgQuery" || name == "pgExec") {
    if (args.size() != 2) throw PTRuntimeError(name + "() expects 2 arguments");
#ifdef HAS_PG
    auto dbArg = evaluate(args[0].get());
    auto sql = evaluate(args[1].get());
    PgResult r;
    if (name == "pgQuery") r = pgQuery(dbArg.db, sql.value);
    else r = pgExec(dbArg.db, sql.value);
    if (!r.ok) throw PTRuntimeError("PostgreSQL error: " + r.error);
    if (name == "pgQuery") {
      auto rows = std::make_shared<std::vector<PTValue>>();
      for (auto& row : r.rows) {
        auto rowMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
        for (auto& [k, v] : row) (*rowMap)[k] = PTValue(v);
        rows->push_back(PTValue(rowMap));
      }
      return PTValue(rows);
    }
    return PT_TRUE;
#else
    throw PTRuntimeError("PostgreSQL support not compiled. Build with 'make pg'");
#endif
  }
  if (name == "pgClose") {
    if (args.size() != 1) throw PTRuntimeError("pgClose() expects 1 argument");
#ifdef HAS_PG
    auto dbArg = evaluate(args[0].get());
    pgClose(dbArg.db);
    return PT_TRUE;
#else
    throw PTRuntimeError("PostgreSQL support not compiled. Build with 'make pg'");
#endif
  }
  return PTValue(std::make_shared<PTFunction>());
}
