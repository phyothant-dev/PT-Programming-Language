#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
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
    if (e->values.count(name)) {
      if (e->consts.count(name)) throw PTRuntimeError("Cannot reassign constant '" + name + "'");
      e->values[name] = value; return;
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

void Interpreter::execute(Stmt& stmt) {
  if (auto* p = dynamic_cast<PrintStmt*>(&stmt)) {
    PTValue val = evaluate(p->expression.get());
    std::cout << formatValue(val) << std::endl;
  } else if (auto* pn = dynamic_cast<PrintNLStmt*>(&stmt)) {
    PTValue val = evaluate(pn->expression.get());
    std::cout << formatValue(val);
  } else if (auto* e = dynamic_cast<ExprStmt*>(&stmt)) {
    PTValue val = evaluate(e->expression.get());
    if (replMode) std::cout << formatValue(val) << std::endl;
  } else if (auto* v = dynamic_cast<VarStmt*>(&stmt)) {
    PTValue val;
    if (v->initializer) val = evaluate(v->initializer.get());
    defineVar(v->name, val);
  } else if (auto* c = dynamic_cast<ConstStmt*>(&stmt)) {
    PTValue val;
    if (c->initializer) val = evaluate(c->initializer.get());
    defineVar(c->name, val);
    env->consts.insert(c->name);
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
    func->name = f->name;
    func->params = f->params;
    func->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>(std::move(f->body));
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
  } else if (auto* rp = dynamic_cast<RepeatStmt*>(&stmt)) {
    for (int i = 0; i < rp->count; i++) {
      try {
        auto repeatEnv = std::make_shared<Environment>(env);
        auto prev = env;
        env = repeatEnv;
        for (auto& s : rp->body) execute(*s);
        env = prev;
      } catch (const BreakException&) { break; }
        catch (const ContinueException&) { continue; }
    }
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
    if (iterable.isFunction) throw PTRuntimeError("for-each requires an array or string");
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
  } else if (auto* ts = dynamic_cast<TryStmt*>(&stmt)) {
    try {
      auto tryEnv = std::make_shared<Environment>(env);
      executeBlock(ts->tryBody, tryEnv);
    } catch (const PTRuntimeError& err) {
      if (!ts->catchBody.empty()) {
        auto catchEnv = std::make_shared<Environment>(env);
        if (!ts->catchVar.empty()) catchEnv->values[ts->catchVar] = PTValue(err.what());
        executeBlock(ts->catchBody, catchEnv);
      }
    } catch (const ReturnException&) {
      if (!ts->finallyBody.empty()) {
        auto finallyEnv = std::make_shared<Environment>(env);
        executeBlock(ts->finallyBody, finallyEnv);
      }
      throw;
    }
    if (!ts->finallyBody.empty()) {
      auto finallyEnv = std::make_shared<Environment>(env);
      executeBlock(ts->finallyBody, finallyEnv);
    }
  } else if (auto* imp = dynamic_cast<ImportStmt*>(&stmt)) {
    std::ifstream file(imp->path);
    if (!file.is_open()) throw PTRuntimeError("Could not import '" + imp->path + "'");
    std::stringstream buf;
    buf << file.rdbuf();
    std::string source = buf.str();
    Lexer lexer(source);
    auto toks = lexer.scan();
    Parser parser(toks);
    auto stmts = parser.parse();
    if (!imp->alias.empty()) {
      auto moduleEnv = std::make_shared<Environment>(globals);
      auto prevEnv = env;
      env = moduleEnv;
      for (auto& s : stmts) execute(*s);
      env = prevEnv;
      auto modMap = std::make_shared<std::unordered_map<std::string, PTValue>>();
      for (auto& [k, v] : moduleEnv->values) (*modMap)[k] = v;
      defineVar(imp->alias, PTValue(modMap));
    } else {
      for (auto& s : stmts) execute(*s);
    }
  } else if (auto* cs = dynamic_cast<ClassStmt*>(&stmt)) {
    auto klass = std::make_shared<PTClass>();
    klass->name = cs->name;
    klass->parentName = cs->parent;

    if (!cs->parent.empty()) {
      auto parentVal = getVar(cs->parent);
      if (!parentVal.isClass) throw PTRuntimeError("'" + cs->parent + "' is not a class");
      klass->parent = parentVal.klass;
    }

    for (auto& sm : cs->staticMethods) {
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

    for (auto& m : cs->methods) {
      auto func = std::make_shared<PTFunction>();
      func->name = m->name;
      func->params = m->params;
      func->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>(std::move(m->body));
      func->closure = env;
      if (m->name == "init") func->isInit = true;
      klass->methods[m->name] = PTValue(func);
    }

    klass->fields = std::move(cs->fields);
    env = prev;

    for (auto& [k, v] : klass->staticMethods) {
      defineVar(k, v);
    }
    defineVar(cs->name, PTValue(klass));
  } else if (auto* es = dynamic_cast<EnumStmt*>(&stmt)) {
    auto m = std::make_shared<std::unordered_map<std::string, PTValue>>();
    for (size_t i = 0; i < es->values.size(); i++) {
      (*m)[es->values[i]] = PTValue(formatNumber(std::to_string((long long)i)));
    }
    defineVar(es->name, PTValue(m));
  } else if (auto* ex = dynamic_cast<ExportStmt*>(&stmt)) {
    execute(*ex->func);
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
  if (dynamic_cast<ThisExpr*>(expr)) {
    return getVar("this");
  }
  if (auto* se = dynamic_cast<SuperExpr*>(expr)) {
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
  if (auto* t = dynamic_cast<TernaryExpr*>(expr)) {
    auto cond = evaluate(t->condition.get());
    if (isTruthy(cond)) return evaluate(t->trueBranch.get());
    return evaluate(t->falseBranch.get());
  }
  if (auto* u = dynamic_cast<Unary*>(expr)) {
    auto right = evaluate(u->right.get());
    if (right.isFunction || right.isArray) throw PTRuntimeError("Cannot use unary on function or array");
    if (u->op == "-") return PTValue(formatNumber(std::to_string(-std::stod(right.value))));
    if (u->op == "!" || u->op == "not") return PTValue(isTruthy(right) ? "false" : "true");
  }
  if (auto* pe = dynamic_cast<PostfixExpr*>(expr)) {
    auto val = evaluate(pe->operand.get());
    if (pe->op == "++") {
      if (val.isFunction || val.isArray || val.isMap) throw PTRuntimeError("Cannot increment non-number");
      double d = std::stod(val.value);
      PTValue result(formatNumber(std::to_string(d + 1)));
      if (auto* v = dynamic_cast<Variable*>(pe->operand.get())) assignVar(v->name, result);
      return result;
    }
    if (pe->op == "--") {
      if (val.isFunction || val.isArray || val.isMap) throw PTRuntimeError("Cannot decrement non-number");
      double d = std::stod(val.value);
      PTValue result(formatNumber(std::to_string(d - 1)));
      if (auto* v = dynamic_cast<Variable*>(pe->operand.get())) assignVar(v->name, result);
      return result;
    }
  }
  if (auto* lc = dynamic_cast<ListCompExpr*>(expr)) {
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
  if (auto* b = dynamic_cast<Binary*>(expr)) {
    auto left = evaluate(b->left.get());
    auto right = evaluate(b->right.get());

    if (b->op == "in") {
      if (right.isArray) {
        for (auto& elem : *right.array)
          if (isEqual(elem, left)) return PTValue("true");
        return PTValue("false");
      }
      if (right.isMap) {
        return PTValue(right.map->count(left.value) ? "true" : "false");
      }
      if (!right.isFunction) {
        return PTValue(right.value.find(left.value) != std::string::npos ? "true" : "false");
      }
      throw PTRuntimeError("'in' requires array, map, or string on right side");
    }

    if (left.isFunction || right.isFunction) throw PTRuntimeError("Cannot use binary on function");

    if (b->op == "+") {
      if (left.isArray || right.isArray) throw PTRuntimeError("Cannot add arrays with +");
      char* e1, *e2;
      double l = std::strtod(left.value.c_str(), &e1);
      double r = std::strtod(right.value.c_str(), &e2);
      if (*e1 == 0 && *e2 == 0) return PTValue(formatNumber(std::to_string(l + r)));
      return PTValue(left.value + right.value);
    }

    if (b->op == "*") {
      if ((left.isArray || right.isArray) && !(left.isArray && right.isArray))
        throw PTRuntimeError("Cannot multiply arrays");
      char* e1, *e2;
      double l = std::strtod(left.value.c_str(), &e1);
      double r = std::strtod(right.value.c_str(), &e2);
      if (*e1 == 0 && *e2 == 0) return PTValue(formatNumber(std::to_string(l * r)));
      if (*e1 == 0 && *e2 != 0 && !right.isArray && !right.isMap && !right.isFunction) {
        std::string result;
        for (int i = 0; i < (int)l; i++) result += right.value;
        return PTValue(result);
      }
      if (*e1 != 0 && *e2 == 0 && !left.isArray && !left.isMap && !left.isFunction) {
        std::string result;
        for (int i = 0; i < (int)r; i++) result += left.value;
        return PTValue(result);
      }
      throw PTRuntimeError("Cannot multiply non-numbers");
    }

    if (b->op == "==") return PTValue(isEqual(left, right) ? "true" : "false");
    if (b->op == "!=") return PTValue(isEqual(left, right) ? "false" : "true");
    if (b->op == "is") return PTValue(isEqual(left, right) ? "true" : "false");
    if (b->op == "isnt") return PTValue(isEqual(left, right) ? "false" : "true");

    if (left.isArray || right.isArray) throw PTRuntimeError("Cannot use arithmetic on arrays");
    if (left.isMap || right.isMap) throw PTRuntimeError("Cannot use arithmetic on maps");
    if (!left.isFunction && !left.isArray && left.value.find_first_not_of("0123456789.eE+-") != std::string::npos)
      throw PTRuntimeError("Left operand must be a number");
    if (!right.isFunction && !right.isArray && right.value.find_first_not_of("0123456789.eE+-") != std::string::npos)
      throw PTRuntimeError("Right operand must be a number");

    if (b->op == "-") return PTValue(formatNumber(std::to_string(std::stod(left.value) - std::stod(right.value))));
    if (b->op == "/") {
      double r = std::stod(right.value);
      if (r == 0) throw PTRuntimeError("Division by zero");
      return PTValue(formatNumber(std::to_string(std::stod(left.value) / r)));
    }
    if (b->op == "%") {
      double r = std::stod(right.value);
      if (r == 0) throw PTRuntimeError("Modulo by zero");
      return PTValue(formatNumber(std::to_string(std::fmod(std::stod(left.value), r))));
    }
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
  if (auto* me = dynamic_cast<MapExpr*>(expr)) {
    auto m = std::make_shared<std::unordered_map<std::string, PTValue>>();
    for (auto& [k, v] : me->entries) {
      auto key = evaluate(k.get());
      if (key.isArray || key.isFunction || key.isMap) throw PTRuntimeError("Map key must be a string");
      (*m)[key.value] = evaluate(v.get());
    }
    return PTValue(m);
  }
  if (auto* de = dynamic_cast<DotExpr*>(expr)) {
    auto obj = evaluate(de->object.get());
    if (obj.isInstance) {
      auto& inst = obj.instance;
      if (inst->fields.count(de->name)) return inst->fields[de->name];
      if (inst->klass->methods.count(de->name)) {
        auto method = inst->klass->methods[de->name].function;
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
        if (inst->klass->parent->methods.count(de->name)) {
          auto method = inst->klass->parent->methods[de->name].function;
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
      if (obj.klass->staticMethods.count(de->name))
        return obj.klass->staticMethods[de->name];
      if (obj.klass->methods.count(de->name))
        return obj.klass->methods[de->name];
      throw PTRuntimeError("Undefined property '" + de->name + "'");
    }
    if (obj.isMap) {
      if (!obj.map->count(de->name)) throw PTRuntimeError("Undefined property '" + de->name + "'");
      return (*obj.map)[de->name];
    }
    throw PTRuntimeError("Cannot access property on non-map, class, or instance");
  }
  if (auto* da = dynamic_cast<DotAssignExpr*>(expr)) {
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
  if (auto* ix = dynamic_cast<IndexExpr*>(expr)) {
    auto callee = evaluate(ix->callee.get());
    auto idx = evaluate(ix->index.get());
    if (callee.isMap) {
      if (idx.isArray || idx.isFunction || idx.isMap) throw PTRuntimeError("Map key must be a string");
      if (!callee.map->count(idx.value)) throw PTRuntimeError("Undefined key '" + idx.value + "'");
      return (*callee.map)[idx.value];
    }
    if (idx.isArray || idx.isFunction) throw PTRuntimeError("Index must be a number");
    int i = (int)std::stod(idx.value);
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
  if (auto* ai = dynamic_cast<AssignIndex*>(expr)) {
    auto callee = evaluate(ai->callee.get());
    if (!callee.isArray) throw PTRuntimeError("Can only assign index into arrays");
    auto idx = evaluate(ai->index.get());
    if (idx.isArray || idx.isFunction) throw PTRuntimeError("Array index must be a number");
    int i = (int)std::stod(idx.value);
    int size = (int)callee.array->size();
    if (i < 0) i += size;
    if (i < 0 || i >= size) throw PTRuntimeError("Index out of bounds");
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

    if (callee.isClass) {
      auto instance = std::make_shared<PTInstance>();
      instance->klass = callee.klass;
      auto instanceVal = PTValue(instance);
      std::vector<PTClass*> chain;
      for (auto* k = callee.klass.get(); k; k = k->parent.get()) chain.push_back(k);
      for (int ci = (int)chain.size() - 1; ci >= 0; ci--) {
        for (auto& [fname, fexpr] : chain[ci]->fields) {
          if (fexpr) {
            instance->fields[fname] = evaluate(fexpr.get());
          } else {
            instance->fields[fname] = PTValue();
          }
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
    if (argCount < paramCount) {
      bool allDefaults = true;
      for (size_t i = argCount; i < paramCount; i++) {
        if (i >= fn->params.size()) { allDefaults = false; break; }
      }
      if (!allDefaults) throw PTRuntimeError("Expected " + std::to_string(paramCount) + " arguments but got " + std::to_string(argCount));
    }
    if (argCount > paramCount) throw PTRuntimeError("Expected " + std::to_string(paramCount) + " arguments but got " + std::to_string(argCount));

    auto callEnv = std::make_shared<Environment>(fn->closure);
    for (size_t i = 0; i < fn->params.size(); i++) {
      if (i < c->arguments.size())
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
  if (auto* te = dynamic_cast<ThrowExpr*>(expr)) {
    auto val = evaluate(te->value.get());
    throw PTRuntimeError(val.value);
  }
  if (auto* le = dynamic_cast<LambdaExpr*>(expr)) {
    auto func = std::make_shared<PTFunction>();
    func->params = le->params;
    func->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>(std::move(le->body));
    func->closure = env;
    return PTValue(func);
  }
  if (auto* ie = dynamic_cast<InterpolatedExpr*>(expr)) {
    std::string result;
    for (size_t i = 0; i < ie->strings.size(); i++) {
      result += ie->strings[i];
      if (i < ie->exprs.size()) {
        auto val = evaluate(ie->exprs[i].get());
        result += formatValue(val);
      }
    }
    return PTValue(result);
  }
  if (auto* me = dynamic_cast<MatchExpr*>(expr)) {
    auto value = evaluate(me->value.get());
    for (auto& mc : me->cases) {
      for (auto& pattern : mc->patterns) {
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
  throw PTRuntimeError("Unknown expression");
}

PTValue Interpreter::evaluateFunction(const PTValue& fnVal, const std::vector<PTValue>& args) {
  if (!fnVal.isFunction) throw PTRuntimeError("Can only call functions");
  auto& fn = fnVal.function;

  if (args.size() < fn->params.size() || args.size() > fn->params.size())
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
    if (arg.isArray) return PTValue(formatNumber(std::to_string((long long)arg.array->size())));
    return PTValue(formatNumber(std::to_string((long long)arg.value.size())));
  }
  if (name == "push") {
    if (args.size() != 2) throw PTRuntimeError("push() expects 2 arguments");
    auto arr = evaluate(args[0].get());
    if (!arr.isArray) throw PTRuntimeError("push() expects an array");
    auto val = evaluate(args[1].get());
    arr.array->push_back(val);
    return PTValue("true");
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
    try {
      return PTValue(formatNumber(arg.value));
    } catch (...) {
      return PTValue("nil");
    }
  }
  if (name == "toString") {
    if (args.size() != 1) throw PTRuntimeError("toString() expects 1 argument");
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
    if (!file.is_open()) return PTValue("false");
    file << content.value;
    return PTValue("true");
  }
  if (name == "abs") {
    if (args.size() != 1) throw PTRuntimeError("abs() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("abs() expects a number");
    return PTValue(formatNumber(std::to_string(std::fabs(std::stod(arg.value)))));
  }
  if (name == "sqrt") {
    if (args.size() != 1) throw PTRuntimeError("sqrt() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("sqrt() expects a number");
    return PTValue(formatNumber(std::to_string(std::sqrt(std::stod(arg.value)))));
  }
  if (name == "min") {
    if (args.size() != 2) throw PTRuntimeError("min() expects 2 arguments");
    auto a = evaluate(args[0].get());
    auto b = evaluate(args[1].get());
    if (a.isArray || a.isFunction || b.isArray || b.isFunction) throw PTRuntimeError("min() expects numbers");
    double da = std::stod(a.value), db = std::stod(b.value);
    return PTValue(formatNumber(std::to_string(da < db ? da : db)));
  }
  if (name == "max") {
    if (args.size() != 2) throw PTRuntimeError("max() expects 2 arguments");
    auto a = evaluate(args[0].get());
    auto b = evaluate(args[1].get());
    if (a.isArray || a.isFunction || b.isArray || b.isFunction) throw PTRuntimeError("max() expects numbers");
    double da = std::stod(a.value), db = std::stod(b.value);
    return PTValue(formatNumber(std::to_string(da > db ? da : db)));
  }
  if (name == "floor") {
    if (args.size() != 1) throw PTRuntimeError("floor() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("floor() expects a number");
    return PTValue(formatNumber(std::to_string((long long)std::floor(std::stod(arg.value)))));
  }
  if (name == "ceil") {
    if (args.size() != 1) throw PTRuntimeError("ceil() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("ceil() expects a number");
    return PTValue(formatNumber(std::to_string((long long)std::ceil(std::stod(arg.value)))));
  }
  if (name == "round") {
    if (args.size() != 1) throw PTRuntimeError("round() expects 1 argument");
    auto arg = evaluate(args[0].get());
    if (arg.isArray || arg.isFunction) throw PTRuntimeError("round() expects a number");
    return PTValue(formatNumber(std::to_string((long long)std::round(std::stod(arg.value)))));
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
    bool isNum = false;
    try { std::stod(arg.value); isNum = true; } catch (...) {}
    if (isNum) return PTValue("number");
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
    return PTValue(obj.map->count(key.value) ? "true" : "false");
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
    int start = (int)std::stod(startArg.value);
    int len = (int)str.value.size() - start;
    if (args.size() == 3) {
      auto lenArg = evaluate(args[2].get());
      len = (int)std::stod(lenArg.value);
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
    return PTValue(str.value.find(sub.value) != std::string::npos ? "true" : "false");
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
      if (args.size() == 2) {
        auto m = evaluate(args[1].get());
        msg = m.value;
      }
      throw PTRuntimeError(msg);
    }
    return PTValue("true");
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
          return PTValue(formatNumber(std::to_string((long long)i)));
      return PTValue("-1");
    }
    if (haystack.isFunction) throw PTRuntimeError("indexOf() expects a string or array");
    if (needle.isArray || needle.isFunction) throw PTRuntimeError("indexOf() needle must be a string");
    auto pos = haystack.value.find(needle.value);
    if (pos == std::string::npos) return PTValue("-1");
    return PTValue(formatNumber(std::to_string((long long)pos)));
  }
  if (name == "sort") {
    if (args.size() != 1) throw PTRuntimeError("sort() expects 1 argument");
    auto arr = evaluate(args[0].get());
    if (!arr.isArray) throw PTRuntimeError("sort() expects an array");
    auto sorted = std::make_shared<std::vector<PTValue>>(*arr.array);
    std::sort(sorted->begin(), sorted->end(), [](const PTValue& a, const PTValue& b) {
      if (a.isFunction || b.isFunction || a.isArray || b.isArray || a.isMap || b.isMap)
        return a.value < b.value;
      try { return std::stod(a.value) < std::stod(b.value); }
      catch (...) { return a.value < b.value; }
    });
    return PTValue(sorted);
  }
  if (name == "range") {
    if (args.size() < 1 || args.size() > 2) throw PTRuntimeError("range() expects 1 or 2 arguments");
    auto arr = std::make_shared<std::vector<PTValue>>();
    if (args.size() == 1) {
      int end = (int)std::stod(evaluate(args[0].get()).value);
      for (int i = 0; i < end; i++) arr->push_back(PTValue(formatNumber(std::to_string((long long)i))));
    } else {
      int start = (int)std::stod(evaluate(args[0].get()).value);
      int end = (int)std::stod(evaluate(args[1].get()).value);
      if (start <= end) for (int i = start; i < end; i++) arr->push_back(PTValue(formatNumber(std::to_string((long long)i))));
      else for (int i = start; i > end; i--) arr->push_back(PTValue(formatNumber(std::to_string((long long)i))));
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
    for (auto& elem : *arr.array) {
      auto val = evaluateFunction(fn, {elem});
      result->push_back(val);
    }
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
      auto val = evaluateFunction(fn, {elem});
      if (isTruthy(val)) result->push_back(elem);
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
      return PTValue(formatNumber(std::to_string(dist(rng))));
    }
    if (args.size() == 1) {
      int max = (int)std::stod(evaluate(args[0].get()).value);
      std::uniform_int_distribution<int> dist(0, max - 1);
      return PTValue(formatNumber(std::to_string((long long)dist(rng))));
    }
    int min = (int)std::stod(evaluate(args[0].get()).value);
    int max = (int)std::stod(evaluate(args[1].get()).value);
    std::uniform_int_distribution<int> dist(min, max - 1);
    return PTValue(formatNumber(std::to_string((long long)dist(rng))));
  }
  if (name == "clock") {
    if (args.size() != 0) throw PTRuntimeError("clock() expects no arguments");
    auto now = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(now.time_since_epoch()).count();
    return PTValue(formatNumber(std::to_string(secs)));
  }
  return PTValue(std::make_shared<PTFunction>());
}

bool Interpreter::isTruthy(const PTValue& val) {
  if (val.isFunction) return true;
  if (val.isArray) return val.array->size() > 0;
  if (val.isMap) return val.map->size() > 0;
  if (val.isClass || val.isInstance) return true;
  if (val.value == "false" || val.value == "nil" || val.value == "0" || val.value == "") return false;
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

std::string Interpreter::formatNumber(const std::string& val) {
  double d = std::stod(val);
  if (d == std::floor(d) && !std::isinf(d) && std::abs(d) < 1e15) return std::to_string((long long)d);
  std::string s = std::to_string(d);
  s.erase(s.find_last_not_of('0') + 1, std::string::npos);
  if (s.back() == '.') s.pop_back();
  return s;
}
