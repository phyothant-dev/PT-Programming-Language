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
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>

static const PTValue PT_TRUE(true);
static const PTValue PT_FALSE(false);
static const PTValue PT_NIL;

class BytecodeCompiler {
  BytecodeChunk chunk;
  StringInterner& interner;
  struct Local { int id; int depth; };
  std::vector<Local> locals;
  int scopeDepth = 0;
  std::vector<size_t> breakJumps;
  std::vector<size_t> continueJumps;
  int peakLocalCount = 0;
  bool failed = false;
  bool isTopLevel = false;

  void emit(Op op) { chunk.emitOp(op); }
  void emitU32(uint32_t v) { chunk.emitU32(v); }
  int addConst(PTValue v) { return chunk.addConst(std::move(v)); }

  int addLocal(int id) {
    locals.push_back({id, scopeDepth});
    if ((int)locals.size() > peakLocalCount) peakLocalCount = (int)locals.size();
    return (int)locals.size() - 1;
  }
  int resolveLocal(int id) {
    for (int i = (int)locals.size() - 1; i >= 0; i--)
      if (locals[i].id == id) return i;
    return -1;
  }
  void beginScope() { scopeDepth++; }
  void endScope() {
    int removed = 0;
    while (!locals.empty() && locals.back().depth >= scopeDepth) {
      locals.pop_back(); removed++;
    }
    scopeDepth--;
    for (int i = 0; i < removed; i++) emit(Op::POP);
  }
  void emitConst(PTValue v) { emit(Op::LOAD_CONST); emitU32(addConst(std::move(v))); }

  void compileStmt(Stmt& stmt) {
    if (failed) return;
    switch (stmt.stype) {
    case StmtType::Var: {
      auto& v = static_cast<VarStmt&>(stmt);
      if (v.initializer) compileExpr(v.initializer.get());
      else emitConst(PT_NIL);
      int id = interner.intern(v.name);
      if (isTopLevel) {
        int idx = addLocal(id);
        emit(Op::STORE_LOCAL); emitU32(idx);
        emit(Op::LOAD_LOCAL); emitU32(idx);
        emit(Op::DEFINE_VAR); emitU32(id);
      } else {
        emit(Op::STORE_LOCAL); emitU32(addLocal(id));
      }
      break;
    }
    case StmtType::Const: {
      auto& c = static_cast<ConstStmt&>(stmt);
      if (c.initializer) compileExpr(c.initializer.get());
      else emitConst(PT_NIL);
      int id = interner.intern(c.name);
      if (isTopLevel) {
        int idx = addLocal(id);
        emit(Op::STORE_LOCAL); emitU32(idx);
        emit(Op::LOAD_LOCAL); emitU32(idx);
        emit(Op::DEFINE_VAR); emitU32(id);
      } else {
        emit(Op::STORE_LOCAL); emitU32(addLocal(id));
      }
      break;
    }
    case StmtType::Expr: {
      auto& e = static_cast<ExprStmt&>(stmt);
      compileExpr(e.expression.get());
      emit(Op::POP);
      break;
    }
    case StmtType::Return: {
      auto& r = static_cast<ReturnStmt&>(stmt);
      if (r.value) compileExpr(r.value.get());
      else emitConst(PT_NIL);
      emit(Op::RETURN);
      break;
    }
    case StmtType::If: {
      auto& i = static_cast<IfStmt&>(stmt);
      compileExpr(i.condition.get());
      size_t falsePatch = chunk.code.size();
      emit(Op::JMP_IF_FALSE); emitU32(0);
      compileStmt(*i.thenBranch);
      if (i.elseBranch) {
        size_t elsePatch = chunk.code.size();
        emit(Op::JMP); emitU32(0);
        chunk.patchJump(falsePatch);
        compileStmt(*i.elseBranch);
        chunk.patchJump(elsePatch);
      } else {
        chunk.patchJump(falsePatch);
      }
      break;
    }
    case StmtType::While: {
      auto& w = static_cast<WhileStmt&>(stmt);
      size_t loopStart = chunk.code.size();
      compileExpr(w.condition.get());
      size_t exitPatch = chunk.code.size();
      emit(Op::JMP_IF_FALSE); emitU32(0);
      size_t prevBreak = breakJumps.size();
      size_t prevContinue = continueJumps.size();
      compileStmt(*w.body);
      emit(Op::JMP);
      emitU32(static_cast<uint32_t>(loopStart - chunk.code.size() - 4));
      chunk.patchJump(exitPatch);
      while (breakJumps.size() > prevBreak) {
        chunk.patchJump(breakJumps.back()); breakJumps.pop_back();
      }
      while (continueJumps.size() > prevContinue) {
        size_t pos = continueJumps.back(); continueJumps.pop_back();
        chunk.patchJumpTo(pos, loopStart);
      }
      break;
    }
    case StmtType::For: {
      auto& fr = static_cast<ForStmt&>(stmt);
      beginScope();
      if (fr.initializer) compileStmt(*fr.initializer);
      size_t loopStart = chunk.code.size();
      if (fr.condition) {
        compileExpr(fr.condition.get());
        size_t exitPatch = chunk.code.size();
        emit(Op::JMP_IF_FALSE); emitU32(0);
        size_t prevBreak = breakJumps.size();
        size_t prevContinue = continueJumps.size();
        compileStmt(*fr.body);
        size_t incrementStart = chunk.code.size();
        if (fr.increment) {
          compileExpr(fr.increment.get());
          emit(Op::POP);
        }
        emit(Op::JMP);
        emitU32(static_cast<uint32_t>(loopStart - chunk.code.size() - 4));
        chunk.patchJump(exitPatch);
        while (breakJumps.size() > prevBreak) {
          chunk.patchJump(breakJumps.back()); breakJumps.pop_back();
        }
        while (continueJumps.size() > prevContinue) {
          size_t pos = continueJumps.back(); continueJumps.pop_back();
          chunk.patchJumpTo(pos, incrementStart);
        }
      } else {
        size_t prevBreak = breakJumps.size();
        size_t prevContinue = continueJumps.size();
        compileStmt(*fr.body);
        size_t incrementStart = chunk.code.size();
        if (fr.increment) {
          compileExpr(fr.increment.get());
          emit(Op::POP);
        }
        emit(Op::JMP);
        emitU32(static_cast<uint32_t>(loopStart - chunk.code.size() - 4));
        while (breakJumps.size() > prevBreak) {
          chunk.patchJump(breakJumps.back()); breakJumps.pop_back();
        }
        while (continueJumps.size() > prevContinue) {
          size_t pos = continueJumps.back(); continueJumps.pop_back();
          chunk.patchJumpTo(pos, incrementStart);
        }
      }
      endScope();
      break;
    }
    case StmtType::Block: {
      auto& b = static_cast<BlockStmt&>(stmt);
      bool prevTopLevel = isTopLevel;
      isTopLevel = false;
      beginScope();
      for (auto& s : b.statements) compileStmt(*s);
      endScope();
      isTopLevel = prevTopLevel;
      break;
    }
    case StmtType::Break: {
      size_t patch = chunk.code.size();
      emit(Op::JMP); emitU32(0);
      breakJumps.push_back(patch);
      break;
    }
    case StmtType::Continue: {
      size_t patch = chunk.code.size();
      emit(Op::JMP); emitU32(0);
      continueJumps.push_back(patch);
      break;
    }
    case StmtType::Print: {
      auto& p = static_cast<PrintStmt&>(stmt);
      compileExpr(p.expression.get());
      emit(Op::PRINT);
      break;
    }
    case StmtType::PrintNL: {
      auto& p = static_cast<PrintNLStmt&>(stmt);
      compileExpr(p.expression.get());
      emit(Op::PRINT_NL);
      break;
    }
    case StmtType::Function: {
      auto& f = static_cast<FunctionStmt&>(stmt);
      std::vector<int> paramIds(f.params.size());
      for (size_t i = 0; i < f.params.size(); i++)
        paramIds[i] = interner.intern(f.params[i]);
      auto fn = std::make_shared<PTFunction>();
      fn->name = f.name;
      fn->params = f.params;
      fn->paramIds = paramIds;
      fn->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>(std::move(f.body));
      bool canCompile = true;
      for (auto& s : *fn->body) {
        switch (s->stype) {
          case StmtType::Var: case StmtType::Expr: case StmtType::Return:
          case StmtType::If: case StmtType::While: case StmtType::Block:
          case StmtType::Break: case StmtType::Continue:
            break;
          default: canCompile = false; break;
        }
      }
      if (canCompile) {
        for (auto& s : *fn->body) {
          if (s->stype == StmtType::Var) {
            auto& v = static_cast<VarStmt&>(*s);
            if (v.initializer && v.initializer->type == ExprType::Call) { canCompile = false; break; }
          }
        }
      }
      if (canCompile) {
        BytecodeCompiler subCompiler(interner);
        fn->bytecode = std::make_shared<BytecodeChunk>(subCompiler.compile(fn->params, fn->paramIds, *fn->body, -1));
      }
      int chunkIdx = addConst(PTValue(fn));
      emit(Op::MAKE_FUNCTION);
      emitU32(chunkIdx);
      int id = interner.intern(f.name);
      if (isTopLevel) {
        emit(Op::DEFINE_VAR); emitU32(id);
      } else {
        emit(Op::STORE_LOCAL); emitU32(addLocal(id));
      }
      break;
    }
    case StmtType::ForEach: {
      auto& fe = static_cast<ForEachStmt&>(stmt);
      beginScope();
      int iterId = interner.intern(fe.variable);
      int iterIdx = addLocal(iterId);
      compileExpr(fe.iterable.get());
      int arrIdx = addLocal(interner.intern("__iter_arr__"));
      emit(Op::STORE_LOCAL); emitU32(arrIdx);
      emitConst(PTValue(0.0));
      int idxId = interner.intern("__iter_idx__");
      int idxIdx = addLocal(idxId);
      emit(Op::STORE_LOCAL); emitU32(idxIdx);
      size_t loopStart = chunk.code.size();
      emit(Op::LOAD_LOCAL); emitU32(idxIdx);
      emit(Op::LOAD_LOCAL); emitU32(arrIdx);
      emit(Op::ARRAY_LEN);
      emit(Op::LT);
      size_t exitPatch = chunk.code.size();
      emit(Op::JMP_IF_FALSE); emitU32(0);
      emit(Op::LOAD_LOCAL); emitU32(arrIdx);
      emit(Op::LOAD_LOCAL); emitU32(idxIdx);
      emit(Op::INDEX_GET);
      emit(Op::STORE_LOCAL); emitU32(iterIdx);
      size_t prevBreak = breakJumps.size();
      size_t prevContinue = continueJumps.size();
      compileStmt(*fe.body);
      size_t incrementStart = chunk.code.size();
      emit(Op::LOAD_LOCAL); emitU32(idxIdx);
      emitConst(PTValue(1.0));
      emit(Op::ADD);
      emit(Op::STORE_LOCAL); emitU32(idxIdx);
      emit(Op::JMP);
      emitU32(static_cast<uint32_t>(loopStart - chunk.code.size() - 4));
      chunk.patchJump(exitPatch);
      while (breakJumps.size() > prevBreak) {
        chunk.patchJump(breakJumps.back()); breakJumps.pop_back();
      }
      while (continueJumps.size() > prevContinue) {
        size_t pos = continueJumps.back(); continueJumps.pop_back();
        chunk.patchJumpTo(pos, incrementStart);
      }
      endScope();
      break;
    }
    default:
      failed = true;
      break;
    }
  }

  void compileExpr(Expr* expr) {
    switch (expr->type) {
    case ExprType::Literal: {
      auto* l = static_cast<Literal*>(expr);
      if (l->isNumber) emitConst(PTValue(std::stod(l->value)));
      else if (l->isBool) emitConst(l->value == "true" ? PT_TRUE : PT_FALSE);
      else if (l->isNil) emitConst(PT_NIL);
      else emitConst(PTValue(l->value));
      break;
    }
    case ExprType::Variable: {
      auto* v = static_cast<Variable*>(expr);
      int id = interner.intern(v->name);
      int idx = resolveLocal(id);
      if (idx >= 0) { emit(Op::LOAD_LOCAL); emitU32(idx); }
      else { emit(Op::LOAD_VAR); emitU32(id); }
      break;
    }
    case ExprType::Binary: {
      auto* b = static_cast<Binary*>(expr);
      if (b->op == "and") {
        compileExpr(b->left.get());
        size_t p = chunk.code.size(); emit(Op::JMP_IF_FALSE); emitU32(0);
        compileExpr(b->right.get()); chunk.patchJump(p);
        break;
      }
      if (b->op == "or") {
        compileExpr(b->left.get());
        size_t p = chunk.code.size(); emit(Op::JMP_IF_TRUE); emitU32(0);
        compileExpr(b->right.get()); chunk.patchJump(p);
        break;
      }
      compileExpr(b->left.get()); compileExpr(b->right.get());
      if (b->op == "+") emit(Op::ADD);
      else if (b->op == "-") emit(Op::SUB);
      else if (b->op == "*") emit(Op::MUL);
      else if (b->op == "/") emit(Op::DIV);
      else if (b->op == "%") emit(Op::MOD);
      else if (b->op == "==" || b->op == "is") emit(Op::EQ);
      else if (b->op == "!=" || b->op == "isnt") emit(Op::NEQ);
      else if (b->op == "<") emit(Op::LT);
      else if (b->op == ">") emit(Op::GT);
      else if (b->op == "<=") emit(Op::LTE);
      else if (b->op == ">=") emit(Op::GTE);
      else { emit(Op::POP); emit(Op::POP); emitConst(PT_NIL); }
      break;
    }
    case ExprType::Unary: {
      auto* u = static_cast<Unary*>(expr);
      compileExpr(u->right.get());
      if (u->op == "-") emit(Op::NEG);
      else if (u->op == "!" || u->op == "not") emit(Op::NOT);
      break;
    }
    case ExprType::Assign: {
      auto* a = static_cast<Assign*>(expr);
      int id = interner.intern(a->name);
      int idx = resolveLocal(id);
      if (idx >= 0) {
        if (a->value->type == ExprType::Binary) {
          auto* b = static_cast<Binary*>(a->value.get());
          if (b->left->type == ExprType::Variable) {
            auto* lv = static_cast<Variable*>(b->left.get());
            if (lv->name == a->name) {
              compileExpr(b->right.get());
              if (b->op == "+") {
                emit(Op::ADD_STORE_LOCAL); emitU32(idx);
                if (isTopLevel) { emit(Op::SYNC_ENV); emitU32(id); emitU32(idx); }
              }
              else if (b->op == "*") {
                emit(Op::MUL); emit(Op::STORE_LOCAL); emitU32(idx);
                if (isTopLevel) { emit(Op::SYNC_ENV); emitU32(id); emitU32(idx); }
                emit(Op::LOAD_LOCAL); emitU32(idx);
              }
              else {
                compileExpr(b->left.get()); emit(Op::ADD); emit(Op::STORE_LOCAL); emitU32(idx);
                if (isTopLevel) { emit(Op::SYNC_ENV); emitU32(id); emitU32(idx); }
                emit(Op::LOAD_LOCAL); emitU32(idx);
              }
              break;
            }
          }
        }
        compileExpr(a->value.get());
        emit(Op::STORE_LOCAL); emitU32(idx);
        if (isTopLevel) { emit(Op::SYNC_ENV); emitU32(id); emitU32(idx); }
        emit(Op::LOAD_LOCAL); emitU32(idx);
      } else {
        if (a->value->type == ExprType::Binary) {
          auto* b = static_cast<Binary*>(a->value.get());
          if (b->left->type == ExprType::Variable) {
            auto* lv = static_cast<Variable*>(b->left.get());
            if (lv->name == a->name && b->op == "+") {
              compileExpr(b->right.get());
              emit(Op::ADD_STORE_GLOBAL); emitU32(id);
              break;
            }
          }
        }
        compileExpr(a->value.get());
        emit(Op::STORE_VAR); emitU32(id);
        emit(Op::LOAD_VAR); emitU32(id);
      }
      break;
    }
    case ExprType::Call: {
      auto* c = static_cast<Call*>(expr);
      if (c->callee->type == ExprType::Variable && c->arguments.size() == 2) {
        auto* v = static_cast<Variable*>(c->callee.get());
        if (v->name == "push") {
          compileExpr(c->arguments[0].get());
          compileExpr(c->arguments[1].get());
          emit(Op::PUSH_ARRAY);
          break;
        }
      }
      compileExpr(c->callee.get());
      for (auto& arg : c->arguments) compileExpr(arg.get());
      emit(Op::CALL); emitU32((uint32_t)c->arguments.size());
      break;
    }
    case ExprType::Grouping:
      compileExpr(static_cast<Grouping*>(expr)->expression.get());
      break;
    case ExprType::PostfixExpr: {
      auto* pe = static_cast<PostfixExpr*>(expr);
      auto* v = static_cast<Variable*>(pe->operand.get());
      int id = interner.intern(v->name);
      int idx = resolveLocal(id);
      if (idx >= 0) {
        emit(pe->op == "++" ? Op::INC_LOCAL : Op::DEC_LOCAL);
        emitU32(idx);
        if (isTopLevel) { emit(Op::SYNC_ENV); emitU32(id); emitU32(idx); }
      } else {
        emit(pe->op == "++" ? Op::INC_GLOBAL : Op::DEC_GLOBAL);
        emitU32(id);
      }
      break;
    }
    case ExprType::TernaryExpr: {
      auto* t = static_cast<TernaryExpr*>(expr);
      compileExpr(t->condition.get());
      size_t fp = chunk.code.size(); emit(Op::JMP_IF_FALSE); emitU32(0);
      compileExpr(t->trueBranch.get());
      size_t ep = chunk.code.size(); emit(Op::JMP); emitU32(0);
      chunk.patchJump(fp);
      compileExpr(t->falseBranch.get());
      chunk.patchJump(ep);
      break;
    }
    case ExprType::Logical: {
      auto* l = static_cast<Logical*>(expr);
      if (l->op == "or") {
        compileExpr(l->left.get());
        size_t p = chunk.code.size(); emit(Op::JMP_IF_TRUE); emitU32(0);
        compileExpr(l->right.get()); chunk.patchJump(p);
      } else {
        compileExpr(l->left.get());
        size_t p = chunk.code.size(); emit(Op::JMP_IF_FALSE); emitU32(0);
        compileExpr(l->right.get()); chunk.patchJump(p);
      }
      break;
    }
    case ExprType::ArrayExpr: {
      auto* ar = static_cast<ArrayExpr*>(expr);
      for (auto& e : ar->elements) compileExpr(e.get());
      emit(Op::ARRAY_NEW); emitU32((uint32_t)ar->elements.size());
      break;
    }
    case ExprType::IndexExpr: {
      auto* ix = static_cast<IndexExpr*>(expr);
      compileExpr(ix->callee.get());
      compileExpr(ix->index.get());
      emit(Op::INDEX_GET);
      break;
    }
    case ExprType::AssignIndex: {
      auto* ai = static_cast<AssignIndex*>(expr);
      compileExpr(ai->callee.get());
      compileExpr(ai->index.get());
      compileExpr(ai->value.get());
      emit(Op::INDEX_SET);
      break;
    }
    case ExprType::MapExpr: {
      auto* me = static_cast<MapExpr*>(expr);
      for (auto& [k, v] : me->entries) {
        compileExpr(k.get());
        compileExpr(v.get());
      }
      emit(Op::MAP_NEW); emitU32((uint32_t)me->entries.size());
      break;
    }
    case ExprType::DotExpr: {
      auto* de = static_cast<DotExpr*>(expr);
      compileExpr(de->object.get());
      int nameId = interner.intern(de->name);
      emit(Op::DOT_GET); emitU32(nameId);
      break;
    }
    case ExprType::DotAssignExpr: {
      auto* da = static_cast<DotAssignExpr*>(expr);
      compileExpr(da->object.get());
      compileExpr(da->value.get());
      int nameId = interner.intern(da->name);
      emit(Op::DOT_SET); emitU32(nameId);
      break;
    }
    case ExprType::LambdaExpr: {
      auto* le = static_cast<LambdaExpr*>(expr);
      std::vector<int> paramIds(le->params.size());
      for (size_t i = 0; i < le->params.size(); i++)
        paramIds[i] = interner.intern(le->params[i]);
      auto fn = std::make_shared<PTFunction>();
      fn->params = le->params;
      fn->paramIds = paramIds;
      fn->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>(std::move(le->body));
      bool canCompile = true;
      for (auto& s : *fn->body) {
        switch (s->stype) {
          case StmtType::Var: case StmtType::Expr: case StmtType::Return:
          case StmtType::If: case StmtType::While: case StmtType::Block:
          case StmtType::Break: case StmtType::Continue:
            break;
          default: canCompile = false; break;
        }
      }
      if (canCompile) {
        for (auto& s : *fn->body) {
          if (s->stype == StmtType::Var) {
            auto& v = static_cast<VarStmt&>(*s);
            if (v.initializer && v.initializer->type == ExprType::Call) { canCompile = false; break; }
          }
        }
      }
      if (canCompile) {
        BytecodeCompiler subCompiler(interner);
        fn->bytecode = std::make_shared<BytecodeChunk>(subCompiler.compile(fn->params, fn->paramIds, *fn->body, -1));
      }
      int chunkIdx = addConst(PTValue(fn));
      emit(Op::MAKE_FUNCTION);
      emitU32(chunkIdx);
      break;
    }
    case ExprType::InterpolatedExpr: {
      auto* ie = static_cast<InterpolatedExpr*>(expr);
      if (ie->strings.empty()) { emitConst(PTValue("")); break; }
      emitConst(PTValue(ie->strings[0]));
      int toStringId = interner.intern("toString");
      for (size_t i = 0; i < ie->exprs.size(); i++) {
        emit(Op::LOAD_VAR); emitU32(toStringId);
        compileExpr(ie->exprs[i].get());
        emit(Op::CALL); emitU32(1);
        emit(Op::ADD);
        if (i + 1 < ie->strings.size()) {
          emitConst(PTValue(ie->strings[i + 1]));
          emit(Op::ADD);
        }
      }
      break;
    }
    default:
      emitConst(PT_NIL);
      break;
    }
  }

public:
  std::function<void(Stmt&)> onUnsupported;
  explicit BytecodeCompiler(StringInterner& interner) : interner(interner) {}

  bool isFailed() const { return failed; }
  int getNumLocals() const { return (int)locals.size(); }
  void setTopLevel(bool v) { isTopLevel = v; }

  static bool canCompileStmts(const std::vector<std::unique_ptr<Stmt>>& stmts) {
    for (auto& s : stmts) {
      switch (s->stype) {
      case StmtType::Class:
      case StmtType::Enum:
      case StmtType::Repeat:
      case StmtType::Try:
      case StmtType::Import:
      case StmtType::Export:
        return false;
      default:
        break;
      }
    }
    return true;
  }

  BytecodeChunk compile(const std::vector<std::string>& params,
                        const std::vector<int>& paramIds,
                        const std::vector<std::unique_ptr<Stmt>>& body,
                        int selfId = -1) {
    for (size_t i = 0; i < params.size(); i++)
      addLocal(paramIds[i]);
    if (selfId >= 0) {
      chunk.selfLocal = addLocal(selfId);
    }
    for (auto& stmt : body) {
      compileStmt(*stmt);
      if (failed) break;
    }
    emitConst(PT_NIL); emit(Op::RETURN);
    chunk.numLocals = peakLocalCount;
    return std::move(chunk);
  }
};

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

PTValue& Interpreter::getVarRef(int id) {
  auto e = env;
  while (e) {
    for (size_t i = 0; i < e->values.size(); i++) {
      if (e->values[i].first == id) return e->values[i].second;
    }
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

bool Interpreter::canCompileBody(const std::vector<std::unique_ptr<Stmt>>& body) {
  for (auto& stmt : body) {
    switch (stmt->stype) {
      case StmtType::Var: case StmtType::Expr: case StmtType::Return:
      case StmtType::If: case StmtType::While: case StmtType::Block:
      case StmtType::Break: case StmtType::Continue:
        break;
      default: return false;
    }
  }
  for (auto& stmt : body) {
    switch (stmt->stype) {
      case StmtType::Var: {
        auto& v = static_cast<VarStmt&>(*stmt);
        if (v.initializer && v.initializer->type == ExprType::Call) return false;
        break;
      }
      default: break;
    }
  }
  return true;
}

std::shared_ptr<BytecodeChunk> Interpreter::compileFunction(
    const std::vector<std::string>& params,
    const std::vector<int>& paramIds,
    const std::vector<std::unique_ptr<Stmt>>& body,
    int selfId) {
  BytecodeCompiler compiler(interner);
  return std::make_shared<BytecodeChunk>(compiler.compile(params, paramIds, body, selfId));
}

static inline double toDouble(const PTValue& v) {
  if (v.isNumber()) return v.numValue;
  if (v.isNil()) throw PTRuntimeError("Cannot convert nil to number");
  if (v.value.empty()) throw PTRuntimeError("Cannot convert (type=" + std::to_string((int)v.type) + ") to number");
  try {
    return std::stod(v.value);
  } catch (...) {
    throw PTRuntimeError("Cannot convert '" + v.ensureStr() + "' to number");
  }
}

PTValue Interpreter::execBytecode(PTFunction* fn, const std::vector<PTValue>& args) {
  auto& chunk = *fn->bytecode;
  PTValue stack[VM_MAX_STACK];
  VMFrame frames[VM_MAX_FRAMES];
  int sp = 0;
  int frameCount = 0;
  BytecodeChunk* curChunk = &chunk;
  size_t ip = 0;
  int localStart = 0;

  frames[0].chunk = &chunk;
  frames[0].returnIp = 0;
  frames[0].returnSp = 0;
  frames[0].localStart = 0;
  frames[0].savedEnv = env;
  frames[0].callerLocalStart = 0;
  frameCount = 1;
  sp = (int)args.size();

  for (size_t i = 0; i < args.size(); i++)
    stack[i] = args[i];

  int numLocals = chunk.numLocals;
  for (int i = (int)args.size(); i < numLocals; i++)
    stack[i] = PT_NIL;
  if (sp < numLocals) sp = numLocals;

  #define VM_READ_U32() curChunk->readU32(ip)
  #define VM_READ_I32() curChunk->readI32(ip)
  #define VM_DISPATCH() goto *dispatch_table[static_cast<uint8_t>(curChunk->code[ip++])]
  #define VM_CASE(name) op_##name

#if defined(__GNUC__) || defined(__clang__)
  PTValue b;
  PTValue v;
  PTValue callee;
  PTValue result;
  PTValue idx;
  PTValue val;
  PTValue obj;
  PTValue rhs;
  double old;
  std::shared_ptr<std::vector<PTValue>> arr;
  std::shared_ptr<std::unordered_map<std::string, PTValue>> m;
  std::shared_ptr<PTFunction> func;
  std::shared_ptr<Environment> e;
  std::shared_ptr<PTFunction> mf;
  std::shared_ptr<Environment> me;
  std::shared_ptr<PTFunction> method;
  static const void* dispatch_table[] = {
    &&VM_CASE(LOAD_CONST), &&VM_CASE(LOAD_LOCAL), &&VM_CASE(STORE_LOCAL),
    &&VM_CASE(LOAD_VAR), &&VM_CASE(STORE_VAR), &&VM_CASE(DEFINE_VAR),
    &&VM_CASE(POP),
    &&VM_CASE(ADD), &&VM_CASE(SUB), &&VM_CASE(MUL), &&VM_CASE(DIV), &&VM_CASE(MOD), &&VM_CASE(NEG),
    &&VM_CASE(EQ), &&VM_CASE(NEQ), &&VM_CASE(LT), &&VM_CASE(GT), &&VM_CASE(LTE), &&VM_CASE(GTE),
    &&VM_CASE(NOT),
    &&VM_CASE(JMP), &&VM_CASE(JMP_IF_FALSE), &&VM_CASE(JMP_IF_TRUE),
    &&VM_CASE(CALL), &&VM_CASE(RETURN),
    &&VM_CASE(PRINT), &&VM_CASE(PRINT_NL),
    &&VM_CASE(ARRAY_NEW), &&VM_CASE(INDEX_GET), &&VM_CASE(INDEX_SET),
    &&VM_CASE(DOT_GET), &&VM_CASE(DOT_SET),
    &&VM_CASE(MAP_NEW),
    &&VM_CASE(MAKE_FUNCTION),
    &&VM_CASE(ARRAY_LEN),
    &&VM_CASE(INC_LOCAL), &&VM_CASE(DEC_LOCAL), &&VM_CASE(ADD_STORE_LOCAL),
    &&VM_CASE(INC_GLOBAL), &&VM_CASE(DEC_GLOBAL), &&VM_CASE(ADD_STORE_GLOBAL),
    &&VM_CASE(PUSH_ARRAY), &&VM_CASE(STRING_APPEND),
    &&VM_CASE(SYNC_ENV)
  };

  VM_DISPATCH();

  VM_CASE(LOAD_CONST):
    stack[sp++] = curChunk->constants[VM_READ_U32()];
    VM_DISPATCH();
  VM_CASE(LOAD_LOCAL): {
    uint32_t idx = VM_READ_U32();
    stack[sp++] = stack[localStart + idx];
    VM_DISPATCH();
  }
  VM_CASE(STORE_LOCAL): {
    uint32_t idx = VM_READ_U32();
    stack[localStart + idx] = stack[--sp];
    int minSp = localStart + curChunk->numLocals;
    if (sp < minSp) sp = minSp;
    VM_DISPATCH();
  }
  VM_CASE(LOAD_VAR): {
    uint32_t id = VM_READ_U32();
    const PTValue* val = findVar(id);
    stack[sp++] = val ? *val : PT_NIL;
    VM_DISPATCH();
  }
  VM_CASE(STORE_VAR): {
    uint32_t id = VM_READ_U32();
    assignVar(id, stack[--sp]);
    VM_DISPATCH();
  }
  VM_CASE(DEFINE_VAR): {
    uint32_t id = VM_READ_U32();
    defineVar(id, stack[--sp]);
    VM_DISPATCH();
  }
  VM_CASE(POP): {
    sp--;
    int minSp = frames[frameCount-1].localStart + curChunk->numLocals;
    if (sp < minSp) sp = minSp;
    VM_DISPATCH();
  }
  VM_CASE(NEG): stack[sp-1].numValue = -stack[sp-1].numValue; VM_DISPATCH();
  VM_CASE(NOT): {
    v = stack[--sp];
    stack[sp++] = v.isBool() ? (v.boolValue ? PT_FALSE : PT_TRUE)
                  : v.isNumber() ? (v.numValue == 0 ? PT_TRUE : PT_FALSE)
                  : v.isNil() ? PT_TRUE
                  : (v.value.empty() || v.value == "false" || v.value == "nil" || v.value == "0" ? PT_TRUE : PT_FALSE);
    VM_DISPATCH();
  }
  VM_CASE(ADD): {
    b = stack[--sp];
    PTValue& a = stack[sp-1];
    if (a.isNumber() && b.isNumber()) a.numValue += b.numValue;
    else if (a.isString() && b.isString()) a.value += b.value;
    else a = PTValue(a.ensureStr() + b.ensureStr());
    VM_DISPATCH();
  }
  VM_CASE(SUB): {
    b = stack[--sp];
    PTValue& a = stack[sp-1];
    if (a.isNumber() && b.isNumber()) a.numValue -= b.numValue;
    else a = PTValue(toDouble(a) - toDouble(b));
    VM_DISPATCH();
  }
  VM_CASE(MUL): {
    b = stack[--sp];
    PTValue& a = stack[sp-1];
    if (a.isNumber() && b.isNumber()) a.numValue *= b.numValue;
    else a = PTValue(toDouble(a) * toDouble(b));
    VM_DISPATCH();
  }
  VM_CASE(DIV): {
    b = stack[--sp];
    PTValue& a = stack[sp-1];
    double r = toDouble(b);
    if (r == 0) throw PTRuntimeError("Division by zero");
    if (a.isNumber() && b.isNumber()) a.numValue /= r;
    else a = PTValue(toDouble(a) / r);
    VM_DISPATCH();
  }
  VM_CASE(MOD): {
    b = stack[--sp];
    PTValue& a = stack[sp-1];
    double r = toDouble(b);
    if (r == 0) throw PTRuntimeError("Modulo by zero");
    a = PTValue(std::fmod(toDouble(a), r));
    VM_DISPATCH();
  }
  VM_CASE(EQ): {
    b = stack[--sp];
    PTValue& a = stack[sp-1];
    bool eq;
    if (a.isNumber() && b.isNumber()) eq = a.numValue == b.numValue;
    else eq = isEqual(a, b);
    stack[sp-1] = eq ? PT_TRUE : PT_FALSE;
    VM_DISPATCH();
  }
  VM_CASE(NEQ): {
    b = stack[--sp];
    PTValue& a = stack[sp-1];
    bool eq;
    if (a.isNumber() && b.isNumber()) eq = a.numValue == b.numValue;
    else eq = isEqual(a, b);
    stack[sp-1] = eq ? PT_FALSE : PT_TRUE;
    VM_DISPATCH();
  }
  VM_CASE(LT): {
    b = stack[--sp];
    PTValue& a = stack[sp-1];
    if (a.isNumber() && b.isNumber()) stack[sp-1] = a.numValue < b.numValue ? PT_TRUE : PT_FALSE;
    else stack[sp-1] = a.ensureStr() < b.ensureStr() ? PT_TRUE : PT_FALSE;
    VM_DISPATCH();
  }
  VM_CASE(GT): {
    b = stack[--sp];
    PTValue& a = stack[sp-1];
    if (a.isNumber() && b.isNumber()) stack[sp-1] = a.numValue > b.numValue ? PT_TRUE : PT_FALSE;
    else stack[sp-1] = a.ensureStr() > b.ensureStr() ? PT_TRUE : PT_FALSE;
    VM_DISPATCH();
  }
  VM_CASE(LTE): {
    b = stack[--sp];
    PTValue& a = stack[sp-1];
    if (a.isNumber() && b.isNumber()) stack[sp-1] = a.numValue <= b.numValue ? PT_TRUE : PT_FALSE;
    else stack[sp-1] = a.ensureStr() <= b.ensureStr() ? PT_TRUE : PT_FALSE;
    VM_DISPATCH();
  }
  VM_CASE(GTE): {
    b = stack[--sp];
    PTValue& a = stack[sp-1];
    if (a.isNumber() && b.isNumber()) stack[sp-1] = a.numValue >= b.numValue ? PT_TRUE : PT_FALSE;
    else stack[sp-1] = a.ensureStr() >= b.ensureStr() ? PT_TRUE : PT_FALSE;
    VM_DISPATCH();
  }
  VM_CASE(JMP): {
    int32_t offset = VM_READ_I32();
    ip += offset;
    VM_DISPATCH();
  }
  VM_CASE(JMP_IF_FALSE): {
    int32_t offset = VM_READ_I32();
    PTValue& v = stack[sp-1];
    if (!isTruthy(v)) ip += offset;
    sp--;
    VM_DISPATCH();
  }
  VM_CASE(JMP_IF_TRUE): {
    int32_t offset = VM_READ_I32();
    PTValue& v = stack[sp-1];
    if (isTruthy(v)) ip += offset;
    sp--;
    VM_DISPATCH();
  }
  VM_CASE(CALL): {
    uint32_t argc = VM_READ_U32();
    callee = stack[sp - 1 - argc];

    if (callee.isFunction() && callee.function->isBuiltin) {
      PTValue* argPtr = &stack[sp - argc];
      sp = sp - 1 - argc;
      stack[sp++] = callBuiltinFast(callee.function->name, argPtr, argc);
    } else if (callee.isFunction() && callee.function->bytecode) {
      auto& cfn = callee.function;
      frames[frameCount].chunk = curChunk;
      frames[frameCount].returnIp = ip;
      frames[frameCount].returnSp = sp - 1 - argc;
      frames[frameCount].savedEnv = env;
      frames[frameCount].callerLocalStart = localStart;
      frameCount++;
      curChunk = cfn->bytecode.get();
      ip = 0;
      env = cfn->closure;
      frames[frameCount-1].localStart = sp - argc;
      localStart = sp - argc;
      int numLocals = curChunk->numLocals;
      for (int i = argc; i < numLocals; i++)
        stack[sp - argc + i] = PT_NIL;
      if (curChunk->selfLocal >= 0)
        stack[sp - argc + curChunk->selfLocal] = callee;
      sp = sp - argc + numLocals;
      VM_DISPATCH();
    } else if (callee.isFunction()) {
      std::vector<PTValue> args(argc);
      for (uint32_t i = 0; i < argc; i++)
        args[i] = std::move(stack[sp - argc + i]);
      sp = sp - 1 - argc;
      stack[sp++] = evaluateFunction(callee, args);
    } else {
      sp = sp - 1 - argc;
      stack[sp++] = PT_NIL;
    }
    VM_DISPATCH();
  }
  VM_CASE(RETURN): {
    result = stack[--sp];
    if (frameCount == 1) {
      return result;
    }
    frameCount--;
    curChunk = frames[frameCount].chunk;
    ip = frames[frameCount].returnIp;
    sp = frames[frameCount].returnSp;
    localStart = frames[frameCount].callerLocalStart;
    env = frames[frameCount].savedEnv;
    stack[sp++] = std::move(result);
    VM_DISPATCH();
  }
  VM_CASE(PRINT): {
    v = stack[--sp];
    std::cout << formatValue(v) << std::endl;
    VM_DISPATCH();
  }
  VM_CASE(PRINT_NL): {
    v = stack[--sp];
    std::cout << formatValue(v);
    VM_DISPATCH();
  }
  VM_CASE(ARRAY_NEW): {
    uint32_t count = VM_READ_U32();
    arr = std::make_shared<std::vector<PTValue>>(count);
    for (uint32_t i = 0; i < count; i++)
      (*arr)[i] = std::move(stack[sp - count + i]);
    sp = sp - count;
    stack[sp++] = PTValue(arr);
    VM_DISPATCH();
  }
  VM_CASE(INDEX_GET): {
    idx = std::move(stack[--sp]);
    callee = std::move(stack[--sp]);
    if (callee.isArray()) {
      int i = idx.isNumber() ? (int)idx.numValue : (int)std::stod(idx.value);
      int size = (int)callee.array->size();
      if (i < 0) i += size;
      if (i < 0 || i >= size) throw PTRuntimeError("Index out of bounds");
      stack[sp++] = (*callee.array)[i];
    } else if (callee.isMap()) {
      auto it = callee.map->find(idx.value);
      if (it == callee.map->end()) throw PTRuntimeError("Undefined key '" + idx.value + "'");
      stack[sp++] = it->second;
    } else {
      int i = idx.isNumber() ? (int)idx.numValue : (int)std::stod(idx.value);
      int size = (int)callee.value.size();
      if (i < 0) i += size;
      if (i < 0 || i >= size) throw PTRuntimeError("String index out of bounds");
      stack[sp++] = PTValue(std::string(1, callee.value[i]));
    }
    VM_DISPATCH();
  }
  VM_CASE(INDEX_SET): {
    val = std::move(stack[--sp]);
    idx = std::move(stack[--sp]);
    callee = std::move(stack[--sp]);
    if (!callee.isArray()) throw PTRuntimeError("Can only assign index into arrays");
    int i = idx.isNumber() ? (int)idx.numValue : (int)std::stod(idx.value);
    int size = (int)callee.array->size();
    if (i < 0) i += size;
    if (i < 0 || i >= size) throw PTRuntimeError("Index out of bounds");
    (*callee.array)[i] = val;
    stack[sp++] = std::move(val);
    VM_DISPATCH();
  }
  VM_CASE(MAP_NEW): {
    uint32_t count = VM_READ_U32();
    m = std::make_shared<std::unordered_map<std::string, PTValue>>();
    for (uint32_t i = 0; i < count; i++) {
      PTValue key = std::move(stack[sp - count * 2 + i * 2]);
      PTValue value = std::move(stack[sp - count * 2 + i * 2 + 1]);
      (*m)[key.value] = std::move(value);
    }
    sp = sp - count * 2;
    stack[sp++] = PTValue(m);
    VM_DISPATCH();
  }
  VM_CASE(DOT_GET): {
    uint32_t nameId = VM_READ_U32();
    obj = std::move(stack[--sp]);
    const std::string& name = interner.name(nameId);
    if (obj.isInstance()) {
      auto it = obj.instance->fields.find(name);
      if (it != obj.instance->fields.end()) { stack[sp++] = it->second; VM_DISPATCH(); }
      auto mit = obj.instance->klass->methods.find(name);
      if (mit != obj.instance->klass->methods.end()) {
        method = mit->second.function;
        mf = std::make_shared<PTFunction>();
        mf->name = method->name; mf->params = method->params; mf->paramIds = method->paramIds;
        mf->body = method->body; mf->closure = method->closure;
        static int thisId = -1;
        if (thisId < 0) thisId = interner.intern("this");
        me = std::make_shared<Environment>(mf->closure);
        me->set(thisId, obj);
        mf->closure = me;
        stack[sp++] = PTValue(mf);
        VM_DISPATCH();
      }
      if (obj.instance->klass->parent) {
        auto pit = obj.instance->klass->parent->methods.find(name);
        if (pit != obj.instance->klass->parent->methods.end()) {
          method = pit->second.function;
          mf = std::make_shared<PTFunction>();
          mf->name = method->name; mf->params = method->params; mf->paramIds = method->paramIds;
          mf->body = method->body; mf->closure = method->closure;
          static int thisId = -1;
          if (thisId < 0) thisId = interner.intern("this");
          me = std::make_shared<Environment>(mf->closure);
          me->set(thisId, obj);
          mf->closure = me;
          stack[sp++] = PTValue(mf);
          VM_DISPATCH();
        }
      }
      throw PTRuntimeError("Undefined property '" + name + "'");
    }
    if (obj.isMap()) {
      auto it = obj.map->find(name);
      if (it != obj.map->end()) { stack[sp++] = it->second; VM_DISPATCH(); }
      throw PTRuntimeError("Undefined key '" + name + "'");
    }
    throw PTRuntimeError("Cannot access property on non-object");
  }
  VM_CASE(DOT_SET): {
    uint32_t nameId = VM_READ_U32();
    val = std::move(stack[--sp]);
    obj = std::move(stack[--sp]);
    const std::string& name = interner.name(nameId);
    if (obj.isInstance()) {
      obj.instance->fields[name] = val;
      stack[sp++] = std::move(val);
      VM_DISPATCH();
    }
    if (obj.isMap()) {
      (*obj.map)[name] = val;
      stack[sp++] = std::move(val);
      VM_DISPATCH();
    }
    throw PTRuntimeError("Cannot set property on non-object");
  }
  VM_CASE(MAKE_FUNCTION): {
    uint32_t chunkIdx = VM_READ_U32();
    auto& proto = curChunk->constants[chunkIdx];
    func = std::make_shared<PTFunction>();
    func->name = proto.function->name;
    func->bytecode = proto.function->bytecode;
    func->body = proto.function->body;
    func->closure = env;
    func->params = proto.function->params;
    func->paramIds = proto.function->paramIds;
    stack[sp++] = PTValue(func);
    VM_DISPATCH();
  }
  VM_CASE(ARRAY_LEN): {
    v = std::move(stack[--sp]);
    if (v.isArray()) stack[sp++] = PTValue(static_cast<double>(v.array->size()));
    else if (v.isString()) stack[sp++] = PTValue(static_cast<double>(v.value.size()));
    else throw PTRuntimeError("len() expects a string or array");
    VM_DISPATCH();
  }
  VM_CASE(INC_LOCAL): {
    uint32_t idx = VM_READ_U32();
    old = stack[localStart + idx].numValue;
    stack[sp++] = PTValue(old);
    stack[localStart + idx] = PTValue(old + 1.0);
    VM_DISPATCH();
  }
  VM_CASE(DEC_LOCAL): {
    uint32_t idx = VM_READ_U32();
    old = stack[localStart + idx].numValue;
    stack[sp++] = PTValue(old);
    stack[localStart + idx] = PTValue(old - 1.0);
    VM_DISPATCH();
  }
  VM_CASE(ADD_STORE_LOCAL): {
    uint32_t target = VM_READ_U32();
    rhs = std::move(stack[--sp]);
    PTValue& lhs = stack[localStart + target];
    if (lhs.isNumber() && rhs.isNumber()) lhs.numValue += rhs.numValue;
    else if (lhs.isString() && rhs.isString()) lhs.value += rhs.value;
    else lhs = PTValue(formatValue(lhs) + formatValue(rhs));
    VM_DISPATCH();
  }
  VM_CASE(INC_GLOBAL): {
    uint32_t id = VM_READ_U32();
    e = env;
    while (e) {
      auto it = e->idxMap.find(id);
      if (it != e->idxMap.end()) {
        double old = e->values[it->second].second.numValue;
        e->values[it->second].second.numValue = old + 1.0;
        stack[sp++] = PTValue(old);
        break;
      }
      e = e->enclosing;
    }
    VM_DISPATCH();
  }
  VM_CASE(DEC_GLOBAL): {
    uint32_t id = VM_READ_U32();
    e = env;
    while (e) {
      auto it = e->idxMap.find(id);
      if (it != e->idxMap.end()) {
        double old = e->values[it->second].second.numValue;
        e->values[it->second].second.numValue = old - 1.0;
        stack[sp++] = PTValue(old);
        break;
      }
      e = e->enclosing;
    }
    VM_DISPATCH();
  }
  VM_CASE(ADD_STORE_GLOBAL): {
    uint32_t id = VM_READ_U32();
    rhs = std::move(stack[--sp]);
    e = env;
    while (e) {
      auto it = e->idxMap.find(id);
      if (it != e->idxMap.end()) {
        PTValue& lhs = e->values[it->second].second;
        if (lhs.isNumber() && rhs.isNumber()) lhs.numValue += rhs.numValue;
        else if (lhs.isString() && rhs.isString()) lhs.value += rhs.value;
        else lhs = PTValue(formatValue(lhs) + formatValue(rhs));
        stack[sp++] = lhs;
        break;
      }
      e = e->enclosing;
    }
    VM_DISPATCH();
  }
  VM_CASE(PUSH_ARRAY): {
    val = std::move(stack[--sp]);
    PTValue& arr = stack[sp - 1];
    if (arr.isArray()) {
      arr.array->push_back(std::move(val));
    }
    VM_DISPATCH();
  }
  VM_CASE(STRING_APPEND): {
    rhs = std::move(stack[--sp]);
    PTValue& lhs = stack[sp - 1];
    if (lhs.isString() && rhs.isString()) lhs.value += rhs.value;
    else if (lhs.isString()) lhs.value += formatValue(rhs);
    else lhs = PTValue(formatValue(lhs) + formatValue(rhs));
    VM_DISPATCH();
  }
  VM_CASE(SYNC_ENV): {
    uint32_t envId = VM_READ_U32();
    uint32_t localIdx = VM_READ_U32();
    env->set(envId, stack[localStart + localIdx]);
    VM_DISPATCH();
  }

#else
  while (true) {
    Op op = static_cast<Op>(curChunk->code[ip++]);

    switch (op) {
    case Op::LOAD_CONST:
      stack[sp++] = curChunk->constants[VM_READ_U32()];
      break;
    case Op::LOAD_LOCAL: {
      uint32_t idx = VM_READ_U32();
      stack[sp++] = stack[localStart + idx];
      break;
    }
    case Op::STORE_LOCAL: {
      uint32_t idx = VM_READ_U32();
      stack[localStart + idx] = stack[--sp];
      int minSp = localStart + curChunk->numLocals;
      if (sp < minSp) sp = minSp;
      break;
    }
    case Op::LOAD_VAR: {
      uint32_t id = VM_READ_U32();
      const PTValue* val = findVar(id);
      stack[sp++] = val ? *val : PT_NIL;
      break;
    }
    case Op::STORE_VAR: {
      uint32_t id = VM_READ_U32();
      assignVar(id, stack[--sp]);
      break;
    }
    case Op::DEFINE_VAR: {
      uint32_t id = VM_READ_U32();
      defineVar(id, stack[--sp]);
      break;
    }
    case Op::POP: {
      sp--;
      int minSp = frames[frameCount-1].localStart + curChunk->numLocals;
      if (sp < minSp) sp = minSp;
      break;
    }
    case Op::NEG: stack[sp-1].numValue = -stack[sp-1].numValue; break;
    case Op::NOT: {
      PTValue v = stack[--sp];
      stack[sp++] = v.isBool() ? (v.boolValue ? PT_FALSE : PT_TRUE)
                    : v.isNumber() ? (v.numValue == 0 ? PT_TRUE : PT_FALSE)
                    : v.isNil() ? PT_TRUE
                    : (v.value.empty() || v.value == "false" || v.value == "nil" || v.value == "0" ? PT_TRUE : PT_FALSE);
      break;
    }
    case Op::ADD: {
      PTValue b = stack[--sp];
      PTValue& a = stack[sp-1];
      if (a.isNumber() && b.isNumber()) a.numValue += b.numValue;
      else if (a.isString() && b.isString()) a.value += b.value;
      else a = PTValue(a.ensureStr() + b.ensureStr());
      break;
    }
    case Op::SUB: {
      PTValue b = stack[--sp];
      PTValue& a = stack[sp-1];
      if (a.isNumber() && b.isNumber()) a.numValue -= b.numValue;
      else a = PTValue(toDouble(a) - toDouble(b));
      break;
    }
    case Op::MUL: {
      PTValue b = stack[--sp];
      PTValue& a = stack[sp-1];
      if (a.isNumber() && b.isNumber()) a.numValue *= b.numValue;
      else a = PTValue(toDouble(a) * toDouble(b));
      break;
    }
    case Op::DIV: {
      PTValue b = stack[--sp];
      PTValue& a = stack[sp-1];
      double r = toDouble(b);
      if (r == 0) throw PTRuntimeError("Division by zero");
      if (a.isNumber() && b.isNumber()) a.numValue /= r;
      else a = PTValue(toDouble(a) / r);
      break;
    }
    case Op::MOD: {
      PTValue b = stack[--sp];
      PTValue& a = stack[sp-1];
      double r = toDouble(b);
      if (r == 0) throw PTRuntimeError("Modulo by zero");
      a = PTValue(std::fmod(toDouble(a), r));
      break;
    }
    case Op::EQ: {
      PTValue b = stack[--sp];
      PTValue& a = stack[sp-1];
      bool eq;
      if (a.isNumber() && b.isNumber()) eq = a.numValue == b.numValue;
      else eq = isEqual(a, b);
      stack[sp-1] = eq ? PT_TRUE : PT_FALSE;
      break;
    }
    case Op::NEQ: {
      PTValue b = stack[--sp];
      PTValue& a = stack[sp-1];
      bool eq;
      if (a.isNumber() && b.isNumber()) eq = a.numValue == b.numValue;
      else eq = isEqual(a, b);
      stack[sp-1] = eq ? PT_FALSE : PT_TRUE;
      break;
    }
    case Op::LT: {
      PTValue b = stack[--sp];
      PTValue& a = stack[sp-1];
      if (a.isNumber() && b.isNumber()) stack[sp-1] = a.numValue < b.numValue ? PT_TRUE : PT_FALSE;
      else stack[sp-1] = a.ensureStr() < b.ensureStr() ? PT_TRUE : PT_FALSE;
      break;
    }
    case Op::GT: {
      PTValue b = stack[--sp];
      PTValue& a = stack[sp-1];
      if (a.isNumber() && b.isNumber()) stack[sp-1] = a.numValue > b.numValue ? PT_TRUE : PT_FALSE;
      else stack[sp-1] = a.ensureStr() > b.ensureStr() ? PT_TRUE : PT_FALSE;
      break;
    }
    case Op::LTE: {
      PTValue b = stack[--sp];
      PTValue& a = stack[sp-1];
      if (a.isNumber() && b.isNumber()) stack[sp-1] = a.numValue <= b.numValue ? PT_TRUE : PT_FALSE;
      else stack[sp-1] = a.ensureStr() <= b.ensureStr() ? PT_TRUE : PT_FALSE;
      break;
    }
    case Op::GTE: {
      PTValue b = stack[--sp];
      PTValue& a = stack[sp-1];
      if (a.isNumber() && b.isNumber()) stack[sp-1] = a.numValue >= b.numValue ? PT_TRUE : PT_FALSE;
      else stack[sp-1] = a.ensureStr() >= b.ensureStr() ? PT_TRUE : PT_FALSE;
      break;
    }
    case Op::JMP: {
      int32_t offset = VM_READ_I32();
      ip += offset;
      break;
    }
    case Op::JMP_IF_FALSE: {
      int32_t offset = VM_READ_I32();
      PTValue& v = stack[sp-1];
      if (!isTruthy(v)) ip += offset;
      sp--;
      break;
    }
    case Op::JMP_IF_TRUE: {
      int32_t offset = VM_READ_I32();
      PTValue& v = stack[sp-1];
      if (isTruthy(v)) ip += offset;
      sp--;
      break;
    }
    case Op::CALL: {
      uint32_t argc = VM_READ_U32();
    callee = stack[sp - 1 - argc];

      if (callee.isFunction() && callee.function->isBuiltin) {
        PTValue* argPtr = &stack[sp - argc];
        sp = sp - 1 - argc;
        stack[sp++] = callBuiltinFast(callee.function->name, argPtr, argc);
      } else if (callee.isFunction() && callee.function->bytecode) {
        auto& cfn = callee.function;
        frames[frameCount].chunk = curChunk;
        frames[frameCount].returnIp = ip;
        frames[frameCount].returnSp = sp - 1 - argc;
        frames[frameCount].savedEnv = env;
        frames[frameCount].callerLocalStart = localStart;
        frameCount++;
        curChunk = cfn->bytecode.get();
        ip = 0;
        env = cfn->closure;
        frames[frameCount-1].localStart = sp - argc;
        localStart = sp - argc;
        int numLocals = curChunk->numLocals;
        for (int i = argc; i < numLocals; i++)
          stack[sp - argc + i] = PT_NIL;
        if (curChunk->selfLocal >= 0)
          stack[sp - argc + curChunk->selfLocal] = callee;
        sp = sp - argc + numLocals;
      } else if (callee.isFunction()) {
        std::vector<PTValue> args(argc);
        for (uint32_t i = 0; i < argc; i++)
          args[i] = std::move(stack[sp - argc + i]);
        sp = sp - 1 - argc;
        stack[sp++] = evaluateFunction(callee, args);
      } else {
        sp = sp - 1 - argc;
        stack[sp++] = PT_NIL;
      }
      break;
    }
    case Op::RETURN: {
      PTValue result = stack[--sp];
      if (frameCount == 1) {
        return result;
      }
      frameCount--;
      curChunk = frames[frameCount].chunk;
      ip = frames[frameCount].returnIp;
      sp = frames[frameCount].returnSp;
      localStart = frames[frameCount].callerLocalStart;
      env = frames[frameCount].savedEnv;
      stack[sp++] = std::move(result);
      break;
    }
    case Op::PRINT: {
      PTValue v = stack[--sp];
      std::cout << formatValue(v) << std::endl;
      break;
    }
    case Op::PRINT_NL: {
      PTValue v = stack[--sp];
      std::cout << formatValue(v);
      break;
    }
    case Op::ARRAY_NEW: {
      uint32_t count = VM_READ_U32();
    arr = std::make_shared<std::vector<PTValue>>(count);
      for (uint32_t i = 0; i < count; i++)
        (*arr)[i] = std::move(stack[sp - count + i]);
      sp = sp - count;
      stack[sp++] = PTValue(arr);
      break;
    }
    case Op::INDEX_GET: {
      PTValue idx = std::move(stack[--sp]);
      PTValue callee = std::move(stack[--sp]);
      if (callee.isArray()) {
        int i = idx.isNumber() ? (int)idx.numValue : (int)std::stod(idx.value);
        int size = (int)callee.array->size();
        if (i < 0) i += size;
        if (i < 0 || i >= size) throw PTRuntimeError("Index out of bounds");
        stack[sp++] = (*callee.array)[i];
      } else if (callee.isMap()) {
        auto it = callee.map->find(idx.value);
        if (it == callee.map->end()) throw PTRuntimeError("Undefined key '" + idx.value + "'");
        stack[sp++] = it->second;
      } else {
        int i = idx.isNumber() ? (int)idx.numValue : (int)std::stod(idx.value);
        int size = (int)callee.value.size();
        if (i < 0) i += size;
        if (i < 0 || i >= size) throw PTRuntimeError("String index out of bounds");
        stack[sp++] = PTValue(std::string(1, callee.value[i]));
      }
      break;
    }
    case Op::INDEX_SET: {
      PTValue val = std::move(stack[--sp]);
      PTValue idx = std::move(stack[--sp]);
      PTValue callee = std::move(stack[--sp]);
      if (!callee.isArray()) throw PTRuntimeError("Can only assign index into arrays");
      int i = idx.isNumber() ? (int)idx.numValue : (int)std::stod(idx.value);
      int size = (int)callee.array->size();
      if (i < 0) i += size;
      if (i < 0 || i >= size) throw PTRuntimeError("Index out of bounds");
      (*callee.array)[i] = val;
      stack[sp++] = std::move(val);
      break;
    }
    case Op::MAP_NEW: {
      uint32_t count = VM_READ_U32();
    m = std::make_shared<std::unordered_map<std::string, PTValue>>();
      for (uint32_t i = 0; i < count; i++) {
        PTValue key = std::move(stack[sp - count * 2 + i * 2]);
        PTValue value = std::move(stack[sp - count * 2 + i * 2 + 1]);
        (*m)[key.value] = std::move(value);
      }
      sp = sp - count * 2;
      stack[sp++] = PTValue(m);
      break;
    }
    case Op::DOT_GET: {
      uint32_t nameId = VM_READ_U32();
      PTValue obj = std::move(stack[--sp]);
      const std::string& name = interner.name(nameId);
      if (obj.isInstance()) {
        auto it = obj.instance->fields.find(name);
        if (it != obj.instance->fields.end()) { stack[sp++] = it->second; break; }
        auto mit = obj.instance->klass->methods.find(name);
        if (mit != obj.instance->klass->methods.end()) {
          auto method = mit->second.function;
          auto mf = std::make_shared<PTFunction>();
          mf->name = method->name; mf->params = method->params; mf->paramIds = method->paramIds;
          mf->body = method->body; mf->closure = method->closure;
          static int thisId = -1;
          if (thisId < 0) thisId = interner.intern("this");
          auto me = std::make_shared<Environment>(mf->closure);
          me->set(thisId, obj);
          mf->closure = me;
          stack[sp++] = PTValue(mf);
          break;
        }
        if (obj.instance->klass->parent) {
          auto pit = obj.instance->klass->parent->methods.find(name);
          if (pit != obj.instance->klass->parent->methods.end()) {
          method = pit->second.function;
          mf = std::make_shared<PTFunction>();
          mf->name = method->name; mf->params = method->params; mf->paramIds = method->paramIds;
          mf->body = method->body; mf->closure = method->closure;
          static int thisId = -1;
          if (thisId < 0) thisId = interner.intern("this");
          me = std::make_shared<Environment>(mf->closure);
            me->set(thisId, obj);
            mf->closure = me;
            stack[sp++] = PTValue(mf);
            break;
          }
        }
        throw PTRuntimeError("Undefined property '" + name + "'");
      }
      if (obj.isMap()) {
        auto it = obj.map->find(name);
        if (it != obj.map->end()) { stack[sp++] = it->second; break; }
        throw PTRuntimeError("Undefined key '" + name + "'");
      }
      throw PTRuntimeError("Cannot access property on non-object");
    }
    case Op::DOT_SET: {
      uint32_t nameId = VM_READ_U32();
      PTValue val = std::move(stack[--sp]);
      PTValue obj = std::move(stack[--sp]);
      const std::string& name = interner.name(nameId);
      if (obj.isInstance()) {
        obj.instance->fields[name] = val;
        stack[sp++] = std::move(val);
        break;
      }
      if (obj.isMap()) {
        (*obj.map)[name] = val;
        stack[sp++] = std::move(val);
        break;
      }
      throw PTRuntimeError("Cannot set property on non-object");
    }
    case Op::MAKE_FUNCTION: {
      uint32_t chunkIdx = VM_READ_U32();
      auto& proto = curChunk->constants[chunkIdx];
      auto func = std::make_shared<PTFunction>();
      func->name = proto.function->name;
      func->bytecode = proto.function->bytecode;
      func->body = proto.function->body;
      func->closure = env;
      func->params = proto.function->params;
      func->paramIds = proto.function->paramIds;
      stack[sp++] = PTValue(func);
      break;
    }
    case Op::ARRAY_LEN: {
      PTValue v = std::move(stack[--sp]);
      if (v.isArray()) stack[sp++] = PTValue(static_cast<double>(v.array->size()));
      else if (v.isString()) stack[sp++] = PTValue(static_cast<double>(v.value.size()));
      else throw PTRuntimeError("len() expects a string or array");
      break;
    }
    case Op::INC_LOCAL: {
      uint32_t idx = VM_READ_U32();
      double old = stack[localStart + idx].numValue;
      stack[sp++] = PTValue(old);
      stack[localStart + idx] = PTValue(old + 1.0);
      break;
    }
    case Op::DEC_LOCAL: {
      uint32_t idx = VM_READ_U32();
      double old = stack[localStart + idx].numValue;
      stack[sp++] = PTValue(old);
      stack[localStart + idx] = PTValue(old - 1.0);
      break;
    }
    case Op::ADD_STORE_LOCAL: {
      uint32_t target = VM_READ_U32();
      PTValue rhs = std::move(stack[--sp]);
      PTValue& lhs = stack[localStart + target];
      if (lhs.isNumber() && rhs.isNumber()) lhs.numValue += rhs.numValue;
      else if (lhs.isString() && rhs.isString()) lhs.value += rhs.value;
      else lhs = PTValue(formatValue(lhs) + formatValue(rhs));
      break;
    }
    case Op::INC_GLOBAL: {
      uint32_t id = VM_READ_U32();
      auto e = env;
      while (e) {
        auto it = e->idxMap.find(id);
        if (it != e->idxMap.end()) {
          double old = e->values[it->second].second.numValue;
          e->values[it->second].second.numValue = old + 1.0;
          stack[sp++] = PTValue(old);
          break;
        }
        e = e->enclosing;
      }
      break;
    }
    case Op::DEC_GLOBAL: {
      uint32_t id = VM_READ_U32();
      auto e = env;
      while (e) {
        auto it = e->idxMap.find(id);
        if (it != e->idxMap.end()) {
          double old = e->values[it->second].second.numValue;
          e->values[it->second].second.numValue = old - 1.0;
          stack[sp++] = PTValue(old);
          break;
        }
        e = e->enclosing;
      }
      break;
    }
    case Op::ADD_STORE_GLOBAL: {
      uint32_t id = VM_READ_U32();
      PTValue rhs = std::move(stack[--sp]);
      auto e = env;
      while (e) {
        auto it = e->idxMap.find(id);
        if (it != e->idxMap.end()) {
          PTValue& lhs = e->values[it->second].second;
          if (lhs.isNumber() && rhs.isNumber()) lhs.numValue += rhs.numValue;
          else if (lhs.isString() && rhs.isString()) lhs.value += rhs.value;
          else lhs = PTValue(formatValue(lhs) + formatValue(rhs));
          stack[sp++] = lhs;
          break;
        }
        e = e->enclosing;
      }
      break;
    }
    case Op::PUSH_ARRAY: {
      PTValue val = std::move(stack[--sp]);
      PTValue& arr = stack[sp - 1];
      if (arr.isArray()) {
        arr.array->push_back(std::move(val));
      }
      break;
    }
    case Op::STRING_APPEND: {
      PTValue rhs = std::move(stack[--sp]);
      PTValue& lhs = stack[sp - 1];
      if (lhs.isString() && rhs.isString()) lhs.value += rhs.value;
      else if (lhs.isString()) lhs.value += formatValue(rhs);
      else lhs = PTValue(formatValue(lhs) + formatValue(rhs));
      break;
    }
    case Op::SYNC_ENV: {
      uint32_t envId = VM_READ_U32();
      uint32_t localIdx = VM_READ_U32();
      env->set(envId, stack[localStart + localIdx]);
      break;
    }
    }
  }
#endif
  #undef VM_READ_U32
  #undef VM_READ_I32
  return PT_NIL;
}

void Interpreter::interpret(std::vector<std::unique_ptr<Stmt>>& stmts) {
  if (!globals) {
    globals = std::make_shared<Environment>();
    env = globals;
    registerBuiltins();
  }

  bool canUseBytecode = BytecodeCompiler::canCompileStmts(stmts);

  if (canUseBytecode) {
    BytecodeCompiler compiler(interner);
    compiler.setTopLevel(true);
    auto savedEnv = env;

    auto bytecode = compiler.compile({}, {}, stmts);
    if (!compiler.isFailed()) {
      auto fn = std::make_shared<PTFunction>();
      fn->closure = savedEnv;
      fn->body = std::make_shared<std::vector<std::unique_ptr<Stmt>>>();
      fn->bytecode = std::make_shared<BytecodeChunk>(std::move(bytecode));
      execBytecode(fn.get());
      return;
    }
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
    if (canCompileBody(f.body)) {
      func->bytecode = compileFunction(func->params, func->paramIds, f.body, -1);
    }
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
    PTValue& ref = getVarRef(id);
    PTValue oldVal = ref;
    if (ref.isNumber()) {
      if (pe->op == "++") ref.numValue += 1.0;
      else ref.numValue -= 1.0;
    } else {
      double d = toDouble(ref);
      if (pe->op == "++") assignVar(id, PTValue(d + 1.0));
      else assignVar(id, PTValue(d - 1.0));
    }
    return oldVal;
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
      std::cerr << "[TREEWALK MUL FALLBACK] left.type=" << (int)left.type << " right.type=" << (int)right.type << std::endl;
      return PTValue(left.numValue * right.numValue);
    }

    if (left.isArray() || left.isMap()) throw PTRuntimeError("Cannot use arithmetic on arrays or maps");
    if (right.isArray() || right.isMap()) throw PTRuntimeError("Cannot use arithmetic on arrays or maps");
    std::cerr << "[TREEWALK BINARY] op=" << b->op << " left.type=" << (int)left.type << " right.type=" << (int)right.type << std::endl;
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
      if (fn->isBuiltin) {
        std::vector<PTValue> argVals;
        for (size_t i = 0; i < c->arguments.size(); i++)
          argVals.push_back(evaluate(c->arguments[i].get()));
        return callBuiltinDirect(fn->name, argVals);
      }
      size_t argCount = c->arguments.size();
      size_t paramCount = fn->params.size();
      if (argCount != paramCount)
        throw PTRuntimeError("Expected " + std::to_string(paramCount) + " arguments but got " + std::to_string(argCount));
      if (fn->bytecode) {
        std::vector<PTValue> args(argCount);
        for (size_t i = 0; i < argCount; i++) args[i] = evaluate(c->arguments[i].get());
        auto savedEnv = env;
        env = fn->closure;
        PTValue result;
        try { result = execBytecode(fn.get(), args); }
        catch (...) { env = savedEnv; throw; }
        env = savedEnv;
        return result;
      }
      auto callEnv = acquireEnv(fn->closure);
      for (size_t i = 0; i < fn->params.size(); i++)
        callEnv->setNew(fn->paramIds[i], evaluate(c->arguments[i].get()));
      auto prev = env;
      env = callEnv;
      try {
        for (auto& stmt : *fn->body) {
          execute(*stmt);
          if (returning) break;
        }
      } catch (...) {
        returning = false;
        returnValue = PTValue();
        env = prev;
        throw;
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
    if (fn->isBuiltin) {
      std::vector<PTValue> argVals;
      for (size_t i = 0; i < c->arguments.size(); i++)
        argVals.push_back(evaluate(c->arguments[i].get()));
      return callBuiltinDirect(fn->name, argVals);
    }
    size_t argCount = c->arguments.size();
    size_t paramCount = fn->params.size();
    if (argCount != paramCount)
      throw PTRuntimeError("Expected " + std::to_string(paramCount) + " arguments but got " + std::to_string(argCount));
    if (fn->bytecode) {
      std::vector<PTValue> args(argCount);
      for (size_t i = 0; i < argCount; i++) args[i] = evaluate(c->arguments[i].get());
      auto savedEnv = env;
      env = fn->closure;
      PTValue result = execBytecode(fn.get(), args);
      env = savedEnv;
      return result;
    }
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
  if (fn->bytecode) {
    auto savedEnv = env;
    env = fn->closure;
    PTValue result;
    try { result = execBytecode(fn.get(), args); }
    catch (...) { env = savedEnv; throw; }
    env = savedEnv;
    return result;
  }
  auto funcEnv = acquireEnv(fn->closure);
  for (size_t i = 0; i < fn->params.size(); i++)
    funcEnv->setNew(fn->paramIds[i], args[i]);
  auto prev = env;
  env = funcEnv;
  try {
    for (auto& stmt : *fn->body) {
      execute(*stmt);
      if (returning) break;
    }
  } catch (...) {
    returning = false;
    returnValue = PTValue();
    env = prev;
    throw;
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

PTValue Interpreter::callBuiltinDirect(const std::string& name, const std::vector<PTValue>& args) {
  if (name == "len") {
    if (args.size() != 1) throw PTRuntimeError("len() expects 1 argument");
    if (args[0].isFunction()) throw PTRuntimeError("len() expects a string or array");
    if (args[0].isArray()) return PTValue(static_cast<double>(args[0].array->size()));
    return PTValue(static_cast<double>(args[0].value.size()));
  }
  if (name == "push") {
    if (args.size() != 2) throw PTRuntimeError("push() expects 2 arguments");
    if (!args[0].isArray()) throw PTRuntimeError("push() expects an array");
    args[0].array->push_back(args[1]);
    return PT_TRUE;
  }
  if (name == "pop") {
    if (args.size() != 1) throw PTRuntimeError("pop() expects 1 argument");
    if (!args[0].isArray()) throw PTRuntimeError("pop() expects an array");
    if (args[0].array->empty()) return PT_NIL;
    auto val = args[0].array->back();
    args[0].array->pop_back();
    return val;
  }
  if (name == "toNum") {
    if (args.size() != 1) throw PTRuntimeError("toNum() expects 1 argument");
    if (args[0].isFunction() || args[0].isArray()) throw PTRuntimeError("toNum() expects a string");
    try { return PTValue(std::stod(args[0].value)); }
    catch (...) { return PT_NIL; }
  }
  if (name == "toString") {
    if (args.size() != 1) throw PTRuntimeError("toString() expects 1 argument");
    return PTValue(formatValue(args[0]));
  }
  if (name == "input") {
    if (args.size() > 0) { std::cout << args[0].value; }
    std::string line;
    if (!std::getline(std::cin, line)) return PT_NIL;
    return PTValue(line);
  }
  if (name == "readFile") {
    if (args.size() != 1) throw PTRuntimeError("readFile() expects 1 argument");
    std::ifstream file(args[0].value);
    if (!file.is_open()) return PT_NIL;
    std::stringstream buf; buf << file.rdbuf(); return PTValue(buf.str());
  }
  if (name == "writeFile") {
    if (args.size() != 2) throw PTRuntimeError("writeFile() expects 2 arguments");
    std::ofstream file(args[0].value);
    if (!file.is_open()) return PT_FALSE;
    file << args[1].value; return PT_TRUE;
  }
  if (name == "abs") {
    if (args.size() != 1) throw PTRuntimeError("abs() expects 1 argument");
    return PTValue(std::fabs(toDouble(args[0])));
  }
  if (name == "sqrt") {
    if (args.size() != 1) throw PTRuntimeError("sqrt() expects 1 argument");
    return PTValue(std::sqrt(toDouble(args[0])));
  }
  if (name == "min") {
    if (args.size() != 2) throw PTRuntimeError("min() expects 2 arguments");
    double da = toDouble(args[0]), db = toDouble(args[1]);
    return PTValue(da < db ? da : db);
  }
  if (name == "max") {
    if (args.size() != 2) throw PTRuntimeError("max() expects 2 arguments");
    double da = toDouble(args[0]), db = toDouble(args[1]);
    return PTValue(da > db ? da : db);
  }
  if (name == "floor") {
    if (args.size() != 1) throw PTRuntimeError("floor() expects 1 argument");
    return PTValue(std::floor(toDouble(args[0])));
  }
  if (name == "ceil") {
    if (args.size() != 1) throw PTRuntimeError("ceil() expects 1 argument");
    return PTValue(std::ceil(toDouble(args[0])));
  }
  if (name == "round") {
    if (args.size() != 1) throw PTRuntimeError("round() expects 1 argument");
    return PTValue(std::round(toDouble(args[0])));
  }
  if (name == "type") {
    if (args.size() != 1) throw PTRuntimeError("type() expects 1 argument");
    switch (args[0].type) {
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
    if (!args[0].isMap()) throw PTRuntimeError("keys() expects a map");
    auto arr = std::make_shared<std::vector<PTValue>>();
    for (auto& [k, v] : *args[0].map) arr->push_back(PTValue(k));
    return PTValue(arr);
  }
  if (name == "values") {
    if (args.size() != 1) throw PTRuntimeError("values() expects 1 argument");
    if (!args[0].isMap()) throw PTRuntimeError("values() expects a map");
    auto arr = std::make_shared<std::vector<PTValue>>();
    for (auto& [k, v] : *args[0].map) arr->push_back(v);
    return PTValue(arr);
  }
  if (name == "has") {
    if (args.size() != 2) throw PTRuntimeError("has() expects 2 arguments");
    if (!args[0].isMap()) throw PTRuntimeError("has() expects a map");
    if (args[1].isArray() || args[1].isFunction() || args[1].isMap()) throw PTRuntimeError("has() key must be a string");
    return args[0].map->count(args[1].value) ? PT_TRUE : PT_FALSE;
  }
  if (name == "upper") {
    if (args.size() != 1) throw PTRuntimeError("upper() expects 1 argument");
    std::string s = args[0].value;
    for (auto& c : s) c = std::toupper(c);
    return PTValue(s);
  }
  if (name == "lower") {
    if (args.size() != 1) throw PTRuntimeError("lower() expects 1 argument");
    std::string s = args[0].value;
    for (auto& c : s) c = std::tolower(c);
    return PTValue(s);
  }
  if (name == "trim") {
    if (args.size() != 1) throw PTRuntimeError("trim() expects 1 argument");
    std::string s = args[0].value;
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return PTValue("");
    size_t end = s.find_last_not_of(" \t\n\r");
    return PTValue(s.substr(start, end - start + 1));
  }
  if (name == "substr") {
    if (args.size() < 2 || args.size() > 3) throw PTRuntimeError("substr() expects 2 or 3 arguments");
    int start = args[1].isNumber() ? (int)args[1].numValue : (int)std::stod(args[1].value);
    int len = (int)args[0].value.size() - start;
    if (args.size() == 3) {
      len = args[2].isNumber() ? (int)args[2].numValue : (int)std::stod(args[2].value);
    }
    if (start < 0 || start >= (int)args[0].value.size()) throw PTRuntimeError("substr() start out of bounds");
    if (start + len > (int)args[0].value.size()) len = (int)args[0].value.size() - start;
    return PTValue(args[0].value.substr(start, len));
  }
  if (name == "contains") {
    if (args.size() != 2) throw PTRuntimeError("contains() expects 2 arguments");
    return args[0].value.find(args[1].value) != std::string::npos ? PT_TRUE : PT_FALSE;
  }
  if (name == "replace") {
    if (args.size() != 3) throw PTRuntimeError("replace() expects 3 arguments");
    std::string result = args[0].value;
    size_t pos = 0;
    while ((pos = result.find(args[1].value, pos)) != std::string::npos) {
      result.replace(pos, args[1].value.size(), args[2].value);
      pos += args[2].value.size();
    }
    return PTValue(result);
  }
  if (name == "split") {
    if (args.size() != 2) throw PTRuntimeError("split() expects 2 arguments");
    auto arr = std::make_shared<std::vector<PTValue>>();
    std::string s = args[0].value;
    std::string d = args[1].value;
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
    if (!isTruthy(args[0])) {
      std::string msg = "Assertion failed";
      if (args.size() == 2) msg = args[1].value;
      throw PTRuntimeError(msg);
    }
    return PT_TRUE;
  }
  if (name == "join") {
    if (args.size() != 2) throw PTRuntimeError("join() expects 2 arguments");
    if (!args[0].isArray()) throw PTRuntimeError("join() expects an array");
    std::string result;
    for (size_t i = 0; i < args[0].array->size(); i++) {
      if (i > 0) result += args[1].value;
      result += formatValue((*args[0].array)[i]);
    }
    return PTValue(result);
  }
  if (name == "indexOf") {
    if (args.size() != 2) throw PTRuntimeError("indexOf() expects 2 arguments");
    if (args[0].isArray()) {
      for (size_t i = 0; i < args[0].array->size(); i++)
        if (isEqual((*args[0].array)[i], args[1]))
          return PTValue(static_cast<double>(i));
      return PTValue(-1.0);
    }
    if (args[0].isFunction()) throw PTRuntimeError("indexOf() expects a string or array");
    auto pos = args[0].value.find(args[1].value);
    if (pos == std::string::npos) return PTValue(-1.0);
    return PTValue(static_cast<double>(pos));
  }
  if (name == "sort") {
    if (args.size() != 1) throw PTRuntimeError("sort() expects 1 argument");
    if (!args[0].isArray()) throw PTRuntimeError("sort() expects an array");
    auto sorted = std::make_shared<std::vector<PTValue>>(*args[0].array);
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
      int end = (int)toDouble(args[0]);
      arr->reserve(end);
      for (int i = 0; i < end; i++) arr->push_back(PTValue(static_cast<double>(i)));
    } else {
      int start = (int)toDouble(args[0]);
      int end = (int)toDouble(args[1]);
      if (start <= end) { arr->reserve(end - start); for (int i = start; i < end; i++) arr->push_back(PTValue(static_cast<double>(i))); }
      else { arr->reserve(start - end); for (int i = start; i > end; i--) arr->push_back(PTValue(static_cast<double>(i))); }
    }
    return PTValue(arr);
  }
  if (name == "map") {
    if (args.size() != 2) throw PTRuntimeError("map() expects 2 arguments");
    if (!args[0].isArray()) throw PTRuntimeError("map() expects an array");
    if (!args[1].isFunction()) throw PTRuntimeError("map() expects a function");
    auto result = std::make_shared<std::vector<PTValue>>();
    for (auto& elem : *args[0].array) result->push_back(evaluateFunction(args[1], {elem}));
    return PTValue(result);
  }
  if (name == "filter") {
    if (args.size() != 2) throw PTRuntimeError("filter() expects 2 arguments");
    if (!args[0].isArray()) throw PTRuntimeError("filter() expects an array");
    if (!args[1].isFunction()) throw PTRuntimeError("filter() expects a function");
    auto result = std::make_shared<std::vector<PTValue>>();
    for (auto& elem : *args[0].array)
      if (isTruthy(evaluateFunction(args[1], {elem}))) result->push_back(elem);
    return PTValue(result);
  }
  if (name == "reduce") {
    if (args.size() < 2 || args.size() > 3) throw PTRuntimeError("reduce() expects 2 or 3 arguments");
    if (!args[0].isArray()) throw PTRuntimeError("reduce() expects an array");
    if (!args[1].isFunction()) throw PTRuntimeError("reduce() expects a function");
    PTValue acc;
    size_t start = 0;
    if (args.size() == 3) acc = args[2];
    else {
      if (args[0].array->empty()) throw PTRuntimeError("reduce() requires initial value for empty array");
      acc = (*args[0].array)[0]; start = 1;
    }
    for (size_t i = start; i < args[0].array->size(); i++)
      acc = evaluateFunction(args[1], {acc, (*args[0].array)[i]});
    return acc;
  }
  if (name == "random") {
    if (args.size() > 2) throw PTRuntimeError("random() expects 0, 1, or 2 arguments");
    static std::mt19937 rng(std::random_device{}());
    if (args.size() == 0) return PTValue(std::uniform_real_distribution<double>(0.0, 1.0)(rng));
    if (args.size() == 1) {
      int max = (int)toDouble(args[0]);
      return PTValue(static_cast<double>(std::uniform_int_distribution<int>(0, max - 1)(rng)));
    }
    int min = (int)toDouble(args[0]);
    int max = (int)toDouble(args[1]);
    return PTValue(static_cast<double>(std::uniform_int_distribution<int>(min, max - 1)(rng)));
  }
  if (name == "clock") {
    if (args.size() != 0) throw PTRuntimeError("clock() expects no arguments");
    return PTValue(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  if (name == "getenv") {
    if (args.size() != 1) throw PTRuntimeError("getenv() expects 1 argument");
    const char* val = std::getenv(args[0].value.c_str());
    return PTValue(val ? std::string(val) : "nil");
  }
  if (name == "fileExists") {
    if (args.size() != 1) throw PTRuntimeError("fileExists() expects 1 argument");
    std::ifstream f(args[0].value);
    return f.good() ? PT_TRUE : PT_FALSE;
  }
  if (name == "sqliteOpen") {
    if (args.size() != 1) throw PTRuntimeError("sqliteOpen() expects 1 argument");
    sqlite3* db;
    if (sqlite3_open(args[0].value.c_str(), &db) != SQLITE_OK) {
      std::string err = sqlite3_errmsg(db); sqlite3_close(db);
      throw PTRuntimeError("Cannot open database: " + err);
    }
    return PTValue(db);
  }
  if (name == "sqliteExec") {
    if (args.size() != 2) throw PTRuntimeError("sqliteExec(db, sql) expects 2 arguments");
    if (!args[0].isDatabase()) throw PTRuntimeError("sqliteExec() expects a database");
    char* errMsg = nullptr;
    if (sqlite3_exec(args[0].db, args[1].value.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
      std::string err = errMsg; sqlite3_free(errMsg);
      throw PTRuntimeError("SQL error: " + err);
    }
    return PT_TRUE;
  }
  if (name == "sqliteQuery") {
    if (args.size() != 2) throw PTRuntimeError("sqliteQuery(db, sql) expects 2 arguments");
    if (!args[0].isDatabase()) throw PTRuntimeError("sqliteQuery() expects a database");
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(args[0].db, args[1].value.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
      throw PTRuntimeError("SQL error: " + std::string(sqlite3_errmsg(args[0].db)));
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
    if (!args[0].isDatabase()) throw PTRuntimeError("sqliteClose() expects a database");
    sqlite3_close(args[0].db);
    return PT_TRUE;
  }
  if (name == "parseJSON") {
    if (args.size() != 1) throw PTRuntimeError("parseJSON() expects 1 argument");
    if (!args[0].isString()) throw PTRuntimeError("parseJSON() expects a string");
    return jsonParse(args[0].value);
  }
  if (name == "toJSON") {
    if (args.size() < 1 || args.size() > 2) throw PTRuntimeError("toJSON() expects 1 or 2 arguments");
    bool pretty = false;
    if (args.size() == 2) pretty = isTruthy(args[1]);
    if (!pretty) return PTValue(jsonSerialize(args[0]));
    std::string json = jsonSerialize(args[0]);
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
    auto resp = httpGet(args[0].value);
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
    std::string ct = "application/json";
    if (args.size() == 3) ct = args[2].value;
    auto resp = httpPost(args[0].value, args[1].value, ct);
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
    std::string ct = "application/json";
    if (args.size() == 3) ct = args[2].value;
    auto resp = httpPut(args[0].value, args[1].value, ct);
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
    auto resp = httpDelete(args[0].value);
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
    std::string algo = "sha256";
    if (args.size() == 2) {
      algo = args[1].value;
    }
    if (algo == "sha256") return PTValue(cryptoSha256(args[0].value));
    if (algo == "md5") return PTValue(cryptoMd5(args[0].value));
    throw PTRuntimeError("hash() supports 'sha256' and 'md5'");
  }
  if (name == "base64Encode") {
    if (args.size() != 1) throw PTRuntimeError("base64Encode() expects 1 argument");
    return PTValue(cryptoBase64Encode(args[0].value));
  }
  if (name == "base64Decode") {
    if (args.size() != 1) throw PTRuntimeError("base64Decode() expects 1 argument");
    return PTValue(cryptoBase64Decode(args[0].value));
  }
  if (name == "uuid") {
    if (args.size() != 0) throw PTRuntimeError("uuid() expects no arguments");
    return PTValue(cryptoUuid());
  }
  if (name == "sleep") {
    if (args.size() != 1) throw PTRuntimeError("sleep() expects 1 argument");
    double millis = args[0].isNumber() ? args[0].numValue : std::stod(args[0].value);
    std::this_thread::sleep_for(std::chrono::milliseconds((int)millis));
    return PT_NIL;
  }
  if (name == "spawn") {
    if (args.size() < 1) throw PTRuntimeError("spawn() expects at least 1 argument");
    if (!args[0].isFunction()) throw PTRuntimeError("spawn() expects a function");
    std::vector<PTValue> spawnArgs;
    for (size_t i = 1; i < args.size(); i++) spawnArgs.push_back(args[i]);
    auto capturedFn = args[0].function;
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
    void* conn = pgOpen(args[0].value);
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
    PgResult r;
    if (name == "pgQuery") r = pgQuery(args[0].db, args[1].value);
    else r = pgExec(args[0].db, args[1].value);
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
    pgClose(args[0].db);
    return PT_TRUE;
#else
    throw PTRuntimeError("PostgreSQL support not compiled. Build with 'make pg'");
#endif
  }
  if (name == "httpListen") {
    throw PTRuntimeError("httpListen() cannot be called from bytecode");
  }
  return PTValue(std::make_shared<PTFunction>());
}

PTValue Interpreter::callBuiltinFast(const std::string& name, const PTValue* args, size_t argc) {
  // Hot builtins inlined for zero-overhead dispatch
  if (name[0] == 'l' && name == "len") {
    if (argc != 1) throw PTRuntimeError("len() expects 1 argument");
    if (args[0].isArray()) return PTValue(static_cast<double>(args[0].array->size()));
    return PTValue(static_cast<double>(args[0].value.size()));
  }
  if (name[0] == 'p' && name == "push") {
    if (argc != 2) throw PTRuntimeError("push() expects 2 arguments");
    if (!args[0].isArray()) throw PTRuntimeError("push() expects an array");
    args[0].array->push_back(args[1]);
    return PT_TRUE;
  }
  if (name[0] == 't' && name == "toString") {
    if (argc != 1) throw PTRuntimeError("toString() expects 1 argument");
    return PTValue(formatValue(args[0]));
  }
  if (name[0] == 'c' && name == "clock") {
    if (argc != 0) throw PTRuntimeError("clock() expects no arguments");
    return PTValue(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  if (name[0] == 't' && name == "type") {
    if (argc != 1) throw PTRuntimeError("type() expects 1 argument");
    switch (args[0].type) {
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
  if (name[0] == 't' && name == "toNum") {
    if (argc != 1) throw PTRuntimeError("toNum() expects 1 argument");
    try { return PTValue(std::stod(args[0].value)); }
    catch (...) { return PT_NIL; }
  }
  if (name[0] == 'r' && name == "range") {
    if (argc < 1 || argc > 2) throw PTRuntimeError("range() expects 1 or 2 arguments");
    auto arr = std::make_shared<std::vector<PTValue>>();
    if (argc == 1) {
      int end = (int)toDouble(args[0]);
      arr->reserve(end);
      for (int i = 0; i < end; i++) arr->push_back(PTValue(static_cast<double>(i)));
    } else {
      int start = (int)toDouble(args[0]);
      int end = (int)toDouble(args[1]);
      if (start <= end) { arr->reserve(end - start); for (int i = start; i < end; i++) arr->push_back(PTValue(static_cast<double>(i))); }
      else { arr->reserve(start - end); for (int i = start; i > end; i--) arr->push_back(PTValue(static_cast<double>(i))); }
    }
    return PTValue(arr);
  }
  if (name[0] == 'p' && name == "pop") {
    if (argc != 1) throw PTRuntimeError("pop() expects 1 argument");
    if (!args[0].isArray()) throw PTRuntimeError("pop() expects an array");
    if (args[0].array->empty()) return PT_NIL;
    auto val = args[0].array->back();
    args[0].array->pop_back();
    return val;
  }
  if (name[0] == 's' && name == "sleep") {
    if (argc != 1) throw PTRuntimeError("sleep() expects 1 argument");
    double millis = args[0].isNumber() ? args[0].numValue : std::stod(args[0].value);
    std::this_thread::sleep_for(std::chrono::milliseconds((int)millis));
    return PT_NIL;
  }
  // Fall through to full dispatch for less common builtins
  std::vector<PTValue> argVec(args, args + argc);
  return callBuiltinDirect(name, argVec);
}

void Interpreter::registerBuiltins() {
  auto makeBuiltin = [this](const std::string& name) {
    auto fn = std::make_shared<PTFunction>();
    fn->name = name;
    fn->isBuiltin = true;
    fn->closure = globals;
    fn->params = {};
    fn->paramIds = {};
    defineVar(interner.intern(name), PTValue(fn));
  };
  makeBuiltin("clock");
  makeBuiltin("toString");
  makeBuiltin("len");
  makeBuiltin("push");
  makeBuiltin("pop");
  makeBuiltin("abs");
  makeBuiltin("sqrt");
  makeBuiltin("min");
  makeBuiltin("max");
  makeBuiltin("floor");
  makeBuiltin("ceil");
  makeBuiltin("round");
  makeBuiltin("type");
  makeBuiltin("range");
  makeBuiltin("sort");
  makeBuiltin("indexOf");
  makeBuiltin("join");
  makeBuiltin("split");
  makeBuiltin("replace");
  makeBuiltin("contains");
  makeBuiltin("substr");
  makeBuiltin("upper");
  makeBuiltin("lower");
  makeBuiltin("trim");
  makeBuiltin("toNum");
  makeBuiltin("keys");
  makeBuiltin("values");
  makeBuiltin("has");
  makeBuiltin("assert");
  makeBuiltin("input");
  makeBuiltin("readFile");
  makeBuiltin("writeFile");
  makeBuiltin("fileExists");
  makeBuiltin("getenv");
  makeBuiltin("random");
  makeBuiltin("parseJSON");
  makeBuiltin("toJSON");
  makeBuiltin("httpGet");
  makeBuiltin("httpPost");
  makeBuiltin("httpPut");
  makeBuiltin("httpDelete");
  makeBuiltin("hash");
  makeBuiltin("base64Encode");
  makeBuiltin("base64Decode");
  makeBuiltin("uuid");
  makeBuiltin("sqliteOpen");
  makeBuiltin("sqliteExec");
  makeBuiltin("sqliteQuery");
  makeBuiltin("sqliteClose");
  makeBuiltin("map");
  makeBuiltin("filter");
  makeBuiltin("reduce");
  makeBuiltin("sleep");
  makeBuiltin("spawn");
  makeBuiltin("pgOpen");
  makeBuiltin("pgQuery");
  makeBuiltin("pgExec");
  makeBuiltin("pgClose");
}
