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
  std::string value;
  std::shared_ptr<PTFunction> function;
  bool isFunction;
  std::shared_ptr<std::vector<PTValue>> array;
  bool isArray;
  std::shared_ptr<std::unordered_map<std::string, PTValue>> map;
  bool isMap;
  std::shared_ptr<PTClass> klass;
  bool isClass;
  std::shared_ptr<PTInstance> instance;
  bool isInstance;
  sqlite3* db;
  bool isDatabase;

  PTValue() : value("nil"), isFunction(false), isArray(false), isMap(false), isClass(false), isInstance(false), db(nullptr), isDatabase(false) {}
  PTValue(std::string v) : value(v), isFunction(false), isArray(false), isMap(false), isClass(false), isInstance(false), db(nullptr), isDatabase(false) {}
  PTValue(std::shared_ptr<PTFunction> f) : value("<fn>"), function(f), isFunction(true), isArray(false), isMap(false), isClass(false), isInstance(false), db(nullptr), isDatabase(false) {}
  PTValue(std::shared_ptr<std::vector<PTValue>> a) : value("<array>"), function(nullptr), isFunction(false), array(a), isArray(true), map(nullptr), isMap(false), isClass(false), isInstance(false), db(nullptr), isDatabase(false) {}
  PTValue(std::shared_ptr<std::unordered_map<std::string, PTValue>> m) : value("<map>"), function(nullptr), isFunction(false), array(nullptr), isArray(false), map(m), isMap(true), isClass(false), isInstance(false), db(nullptr), isDatabase(false) {}
  PTValue(std::shared_ptr<PTClass> c) : value("<class>"), isFunction(false), isArray(false), isMap(false), klass(c), isClass(true), isInstance(false), db(nullptr), isDatabase(false) {}
  PTValue(std::shared_ptr<PTInstance> i) : value("<instance>"), isFunction(false), isArray(false), isMap(false), isClass(false), instance(i), isInstance(true), db(nullptr), isDatabase(false) {}
  PTValue(sqlite3* d) : value("<database>"), isFunction(false), isArray(false), isMap(false), isClass(false), isInstance(false), db(d), isDatabase(true) {}
};

struct Environment {
  std::unordered_map<std::string, PTValue> values;
  std::unordered_set<std::string> consts;
  std::shared_ptr<Environment> enclosing;
  Environment() = default;
  Environment(std::shared_ptr<Environment> enc) : enclosing(enc) {}
};

struct PTFunction {
  std::string name;
  std::vector<std::string> params;
  std::shared_ptr<std::vector<std::unique_ptr<Stmt>>> body;
  std::shared_ptr<Environment> closure;
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

class Interpreter {
public:
  void interpret(std::vector<std::unique_ptr<Stmt>>& stmts);
  bool replMode = false;

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
  PTValue evaluateFunction(const PTValue& fn, const std::vector<PTValue>& args);
  PTValue callBuiltin(const std::string& name, const std::vector<std::unique_ptr<Expr>>& args);
  std::string formatValue(const PTValue& val);
  bool isTruthy(const PTValue& val);
  bool isEqual(const PTValue& a, const PTValue& b);
  std::string formatNumber(const std::string& val);
};
