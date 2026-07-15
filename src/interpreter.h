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

class StringInterner {
  std::unordered_map<std::string, int> nameToId;
  std::vector<std::string> idToName;
public:
  int intern(const std::string& name) {
    auto [it, ok] = nameToId.emplace(name, (int)idToName.size());
    if (ok) idToName.push_back(name);
    return it->second;
  }
  const std::string& name(int id) const { return idToName[id]; }
  int count() const { return (int)idToName.size(); }
};

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

enum class Op : uint8_t {
  LOAD_CONST, LOAD_LOCAL, STORE_LOCAL,
  LOAD_VAR, STORE_VAR,
  POP,
  ADD, SUB, MUL, DIV, MOD, NEG,
  EQ, NEQ, LT, GT, LTE, GTE,
  NOT,
  JMP, JMP_IF_FALSE, JMP_IF_TRUE,
  CALL, RETURN
};

struct BytecodeChunk {
  std::vector<uint8_t> code;
  std::vector<PTValue> constants;

  void emitOp(Op op) { code.push_back(static_cast<uint8_t>(op)); }
  void emitU32(uint32_t v) {
    code.push_back(static_cast<uint8_t>(v));
    code.push_back(static_cast<uint8_t>(v >> 8));
    code.push_back(static_cast<uint8_t>(v >> 16));
    code.push_back(static_cast<uint8_t>(v >> 24));
  }
  void emitI32(int32_t v) { emitU32(static_cast<uint32_t>(v)); }
  uint32_t readU32(size_t& ip) const {
    uint32_t v = static_cast<uint32_t>(code[ip])
      | (static_cast<uint32_t>(code[ip+1]) << 8)
      | (static_cast<uint32_t>(code[ip+2]) << 16)
      | (static_cast<uint32_t>(code[ip+3]) << 24);
    ip += 4; return v;
  }
  int32_t readI32(size_t& ip) const { return static_cast<int32_t>(readU32(ip)); }
  int addConst(PTValue v) {
    int idx = static_cast<int>(constants.size());
    constants.push_back(std::move(v)); return idx;
  }
  void patchJump(size_t offset) {
    int32_t dist = static_cast<int32_t>(code.size()) - static_cast<int32_t>(offset) - 5;
    code[offset+1] = static_cast<uint8_t>(dist);
    code[offset+2] = static_cast<uint8_t>(dist >> 8);
    code[offset+3] = static_cast<uint8_t>(dist >> 16);
    code[offset+4] = static_cast<uint8_t>(dist >> 24);
  }
};

struct PTFunction {
  std::string name;
  std::vector<std::string> params;
  std::vector<int> paramIds;
  std::shared_ptr<std::vector<std::unique_ptr<Stmt>>> body;
  std::shared_ptr<class Environment> closure;
  std::shared_ptr<BytecodeChunk> bytecode;
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
  std::vector<std::pair<int, PTValue>> values;
  std::vector<int> consts;
  std::shared_ptr<Environment> enclosing;
  Environment() : enclosing(nullptr) {}
  Environment(std::shared_ptr<Environment> enc) : enclosing(enc) {}
  explicit Environment(std::shared_ptr<Environment> enc, size_t reserveSlots) : enclosing(enc) {
    values.reserve(reserveSlots);
  }

  void reset(std::shared_ptr<Environment> enc) {
    values.clear();
    consts.clear();
    enclosing = enc;
  }

  void set(int id, PTValue val) {
    for (auto& [k, v] : values) {
      if (k == id) { v = std::move(val); return; }
    }
    values.emplace_back(id, std::move(val));
  }

  void setNew(int id, PTValue val) {
    values.emplace_back(id, std::move(val));
  }

  PTValue* find(int id) {
    for (auto& [k, v] : values) {
      if (k == id) return &v;
    }
    return nullptr;
  }

  void addConst(int id) {
    for (auto& c : consts) { if (c == id) return; }
    consts.push_back(id);
  }

  bool isConst(int id) {
    for (auto& c : consts) { if (c == id) return true; }
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
  StringInterner interner;

private:
  bool returning = false;
  PTValue returnValue;
  bool breaking = false;
  bool continuing = false;

  std::vector<Environment*> envPool;
  std::shared_ptr<Environment> acquireEnv(std::shared_ptr<Environment> enclosing) {
    Environment* raw;
    if (!envPool.empty()) { raw = envPool.back(); envPool.pop_back(); }
    else { raw = new Environment(); }
    raw->reset(enclosing);
    return std::shared_ptr<Environment>(raw, [this](Environment* e) { envPool.push_back(e); });
  }

  int internCached(const std::string& name, int& cachedId) {
    if (cachedId < 0) cachedId = interner.intern(name);
    return cachedId;
  }

  void defineVar(int id, PTValue value);
  void assignVar(int id, const PTValue& value);
  const PTValue& getVar(int id);
  const PTValue* findVar(int id);
  bool varExists(int id);

  void execute(Stmt& stmt);
  void executeBlock(std::vector<std::unique_ptr<Stmt>>& stmts, std::shared_ptr<Environment> blockEnv);

  PTValue evaluate(Expr* expr);
  PTValue callBuiltin(const std::string& name, const std::vector<std::unique_ptr<Expr>>& args);
  PTValue callBuiltinDirect(const std::string& name, const std::vector<PTValue>& args);
  std::string formatValue(const PTValue& val);
  bool isTruthy(const PTValue& val);
  bool isEqual(const PTValue& a, const PTValue& b);

  struct VMFrame {
    int localStart;
    BytecodeChunk* chunk;
    size_t returnIp;
    int returnSp;
  };
  static const int VM_MAX_FRAMES = 256;
  static const int VM_MAX_STACK = 4096;
  PTValue execBytecode(PTFunction* fn, const std::vector<PTValue>& args = {});
  std::shared_ptr<BytecodeChunk> compileFunction(
    const std::vector<std::string>& params,
    const std::vector<int>& paramIds,
    const std::vector<std::unique_ptr<Stmt>>& body);
  bool canCompileBody(const std::vector<std::unique_ptr<Stmt>>& body);
};
