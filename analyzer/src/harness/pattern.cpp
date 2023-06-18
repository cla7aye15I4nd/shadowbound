#include "pattern.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <set>
#include <string>

using namespace std;
using namespace llvm;

static bool isHeapAddress(Value *V) {
  set<Value *> Visited;
  vector<Value *> Worklist;

  Worklist.push_back(V);
  while (!Worklist.empty()) {
    Value *V = Worklist.back();
    Worklist.pop_back();

    if (Visited.find(V) != Visited.end())
      continue;
    Visited.insert(V);

    if (auto *GEP = dyn_cast<GetElementPtrInst>(V))
      Worklist.push_back(GEP->getPointerOperand());
    else if (auto *BC = dyn_cast<BitCastInst>(V))
      Worklist.push_back(BC->getOperand(0));
    else if (auto *PHI = dyn_cast<PHINode>(V))
      for (unsigned int i = 0; i < PHI->getNumIncomingValues(); i++)
        Worklist.push_back(PHI->getIncomingValue(i));
    else if (!(isa<GlobalValue>(V) || isa<AllocaInst>(V) || isa<Constant>(V)))
      return true;
  }

  return true;
}

static bool alwaysNonHeap(Function &FO, unsigned int ArgNo,
                          vector<Module *> &Modules,
                          DenseMap<Function *, vector<CallBase *>> &CallSites) {

  for (auto *CI : CallSites[&FO]) {
    Value *Arg = CI->getArgOperand(ArgNo);
    if (isHeapAddress(Arg))
      return false;
  }

  return true;
}

static void findNonHeapFunctionArguments(vector<Module *> Modules,
                                         vector<PatternBase *> &Patterns) {
  DenseMap<Function *, vector<CallBase *>> CallSites;
  for (auto *M : Modules)
    for (auto &F : *M)
      for (auto &BB : F)
        for (auto &I : BB)
          if (auto *CI = dyn_cast<CallBase>(&I))
            if (CI->getCalledFunction() != nullptr)
              CallSites[CI->getCalledFunction()].push_back(CI);

  for (auto *M : Modules) {
    for (auto &F : *M) {
      if (F.isDeclaration())
        continue;
      for (unsigned int i = 0; i < F.arg_size(); i++) {
        if (F.getArg(i)->getType()->isPointerTy() &&
            alwaysNonHeap(F, i, Modules, CallSites)) {
          Patterns.push_back(
              new ValuePattern(new FunArgIdent(F.getName().str(), i)));
        }
      }
    }
  }
}

void dumpPatternOptFile(string Filename, vector<Module *> &Modules) {
  vector<PatternBase *> Patterns;
  findNonHeapFunctionArguments(Modules, Patterns);

  json::Array JSONPatterns;
  for (auto *P : Patterns)
    JSONPatterns.push_back(P->toJSON());

  error_code EC;
  raw_fd_ostream OS(Filename, EC, sys::fs::OF_None);
  if (EC) {
    errs() << "Error: " << EC.message() << "\n";
    exit(1);
  }

  OS << json::Value(std::move(JSONPatterns));
  OS.close();
}