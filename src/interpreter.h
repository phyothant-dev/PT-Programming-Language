#pragma once
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <stdexcept>
#include "ast.h"
#include <sqlite3.h>

struct PTRuntimeError : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct PTFunction;
struct PTClass;
struct PTInstance;

struct PTValue {
  enum Type { TNil, TBool, TNumber, TString, TFunction, TArray, TMap, TClass, TInstance, TDatabase };
  Type type;
  double numValue;
  bool boolValue;
  std::string value;
  std::shared_ptr<PTFunction> function;
  std::shared_ptr<std::vector<PTValue>> array;
  std::shared_ptr<std::unordered_map<std::string, PTValue>> map;
  std::shared_ptr<PTClass> klass;
  std::shared_ptr<PTInstance> instance;
  sqlite3* db;

  bool isNumber() const { return type == TNumber; }
  bool isFunction() const { return type == TFunction; }
  bool isArray() const { return type == TArray; }
  bool isMap() const { return type == TMap; }
  bool isClass() const { return type == TClass; }
  bool isInstance() const { return type == TInstance; }
  bool isDatabase() const { return type == TDatabase; }
  bool isBool() const { return type == TBool; }
  bool isNil() const { return type == TNil; }
  bool isString() const { return type == TString; }

  const std::string& str() const {
    return value;
  }

  std::string ensureStr() const {
    if (type == TNumber) {
      if (numValue == static_cast<long long>(numValue) && numValue >= -1e15 && numValue <= 1e15)
        return std::to_string(static_cast<long long>(numValue));
      char buf[64];
      snprintf(buf, sizeof(buf), "%.6g", numValue);
      return std::string(buf);
    }
    return value;
  }

  PTValue() : type(TNil), numValue(0), boolValue(false), db(nullptr) {}
  PTValue(const char* v) : type(TString), numValue(0), boolValue(false), value(v), db(nullptr) {}
  PTValue(std::string v) : type(TString), numValue(0), boolValue(false), value(std::move(v)), db(nullptr) {}
  PTValue(double n) : type(TNumber), numValue(n), boolValue(false), db(nullptr) {}
  PTValue(long long n) : type(TNumber), numValue(static_cast<double>(n)), boolValue(false), db(nullptr) {}
  PTValue(int n) : type(TNumber), numValue(static_cast<double>(n)), boolValue(false), db(nullptr) {}
  PTValue(bool b) : type(TBool), numValue(0), boolValue(b), db(nullptr) {}
  PTValue(std::shared_ptr<PTFunction> f) : type(TFunction), numValue(0), boolValue(false), function(std::move(f)), db(nullptr) {}
  PTValue(std::shared_ptr<std::vector<PTValue>> a) : type(TArray), numValue(0), boolValue(false), array(std::move(a)), db(nullptr) {}
  PTValue(std::shared_ptr<std::unordered_map<std::string, PTValue>> m) : type(TMap), numValue(0), boolValue(false), map(std::move(m)), db(nullptr) {}
  PTValue(std::shared_ptr<PTClass> c) : type(TClass), numValue(0), boolValue(false), klass(std::move(c)), db(nullptr) {}
  PTValue(std::shared_ptr<PTInstance> i) : type(TInstance), numValue(0), boolValue(false), instance(std::move(i)), db(nullptr) {}
  PTValue(sqlite3* d) : type(TDatabase), numValue(0), boolValue(false), db(d) {}

  PTValue(PTValue&& o) noexcept
    : type(o.type), numValue(o.numValue), boolValue(o.boolValue),
      value(std::move(o.value)), function(std::move(o.function)),
      array(std::move(o.array)), map(std::move(o.map)),
      klass(std::move(o.klass)), instance(std::move(o.instance)), db(o.db) {
    o.type = TNil; o.db = nullptr;
  }

  PTValue& operator=(PTValue&& o) noexcept {
    if (this != &o) {
      type = o.type; numValue = o.numValue; boolValue = o.boolValue;
      value = std::move(o.value); function = std::move(o.function);
      array = std::move(o.array); map = std::move(o.map);
      klass = std::move(o.klass); instance = std::move(o.instance); db = o.db;
      o.type = TNil; o.db = nullptr;
    }
    return *this;
  }

  PTValue(const PTValue& o)
    : type(o.type), numValue(o.numValue), boolValue(o.boolValue),
      function(o.function), array(o.array), map(o.map),
      klass(o.klass), instance(o.instance), db(o.db) {
    if (type != TNumber) value = o.value;
  }

  PTValue& operator=(const PTValue& o) {
    if (this != &o) {
      type = o.type; numValue = o.numValue; boolValue = o.boolValue;
      function = o.function; array = o.array; map = o.map;
      klass = o.klass; instance = o.instance; db = o.db;
      if (type != TNumber) value = o.value;
      else value.clear();
    }
    return *this;
  }
};

struct PTFunction {
  std::string name;
  std::vector<std::string> params;
  std::shared_ptr<std::vector<std::unique_ptr<Stmt>>> body;
  std::shared_ptr<class Environment> closure;
  bool isStatic = false;
  bool isInit = false;
};

struct PTClass {
  std::string name;
  std::string parentName;
  std::shared_ptr<PTClass> parent;
  std::unordered_map<std::string, PTValue> methods;
  std::unordered_map<std::string, PTValue> staticMethods;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
};

struct PTInstance {
  std::shared_ptr<PTClass> klass;
  std::unordered_map<std::string, PTValue> fields;
};

class Environment {
public:
  std::vector<std::pair<std::string, PTValue>> values;
  std::vector<std::string> consts;
  std::shared_ptr<Environment> enclosing;
  Environment() : enclosing(nullptr) {}
  Environment(std::shared_ptr<Environment> enc) : enclosing(enc) {}
  explicit Environment(std::shared_ptr<Environment> enc, size_t reserveSlots) : enclosing(enc) {
    values.reserve(reserveSlots);
  }

  void set(const std::string& name, PTValue val) {
    for (auto& [k, v] : values) {
      if (k == name) { v = std::move(val); return; }
    }
    values.emplace_back(name, std::move(val));
  }

  PTValue* find(const std::string& name) {
    for (auto& [k, v] : values) {
      if (k == name) return &v;
    }
    return nullptr;
  }

  void addConst(const std::string& name) {
    for (auto& c : consts) { if (c == name) return; }
    consts.push_back(name);
  }

  bool isConst(const std::string& name) {
    for (auto& c : consts) { if (c == name) return true; }
    return false;
  }
};

class Interpreter {
public:
  void interpret(std::vector<std::unique_ptr<Stmt>>& stmts);
  bool replMode = false;
  std::shared_ptr<Environment> globals;
  std::shared_ptr<Environment> env;
  PTValue evaluateFunction(const PTValue& fn, const std::vector<PTValue>& args);

private:
  bool returning = false;
  PTValue returnValue;
  bool breaking = false;
  bool continuing = false;

  void defineVar(const std::string& name, PTValue value);
  void assignVar(const std::string& name, const PTValue& value);
  const PTValue& getVar(const std::string& name);
  bool varExists(const std::string& name);

  void execute(Stmt& stmt);
  void executeBlock(std::vector<std::unique_ptr<Stmt>>& stmts, std::shared_ptr<Environment> blockEnv);

  PTValue evaluate(Expr* expr);
  PTValue callBuiltin(const std::string& name, const std::vector<std::unique_ptr<Expr>>& args);
  std::string formatValue(const PTValue& val);
  bool isTruthy(const PTValue& val);
  bool isEqual(const PTValue& a, const PTValue& b);
};
