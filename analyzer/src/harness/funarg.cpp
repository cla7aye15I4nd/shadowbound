#include "pattern.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>

using namespace std;
using namespace llvm;

using func = pair<string, string>;

static map<func, vector<CallBase *>> CallSites;
static set<func> ICallFuncs;

static string getFileName(string Path) {
  size_t LastSlash = Path.find_last_of('/');
  size_t LastDot = Path.find_last_of('.');
  string result = Path.substr(LastSlash + 1, LastDot - LastSlash - 1);

  assert(result != "");
  return result;
}

static func makeFnHash(Function *F) {
  if (F->hasLocalLinkage()) {
    assert(F->getParent()->getModuleIdentifier() != "");
    return func(F->getParent()->getModuleIdentifier(), F->getName().str());
  } else
    return func("", F->getName().str());
}

static void initialize(vector<Module *> &Modules) {
  for (auto *M : Modules)
    for (auto &F : *M) {
      for (auto &BB : F) {
        for (auto &I : BB) {
          if (auto *CI = dyn_cast<CallBase>(&I)) {
            if (auto *Fn = CI->getCalledFunction()) {
              CallSites[makeFnHash(Fn)].push_back(CI);
            }
            for (unsigned int i = 0; i < CI->arg_size(); i++)
              if (auto *Fn = dyn_cast<Function>(CI->getArgOperand(i)))
                ICallFuncs.insert(makeFnHash(Fn));
          } else {
            for (unsigned int i = 0; i < I.getNumOperands(); i++)
              if (auto *Fn = dyn_cast<Function>(I.getOperand(i)))
                ICallFuncs.insert(makeFnHash(Fn));
          }
        }
      }
    }
}

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
    else if (auto *GEPO = dyn_cast<GEPOperator>(V))
      Worklist.push_back(GEPO->getPointerOperand());
    else if (auto *BCO = dyn_cast<BitCastOperator>(V))
      Worklist.push_back(BCO->getOperand(0));
    else if (auto *PHI = dyn_cast<PHINode>(V))
      for (unsigned int i = 0; i < PHI->getNumIncomingValues(); i++)
        Worklist.push_back(PHI->getIncomingValue(i));
    else if (!(isa<GlobalValue>(V) || isa<AllocaInst>(V) || isa<Constant>(V)))
      return true;
  }

  return false;
}

static bool isSafeConstArray(Function &F, Value *V) {
  while (auto *BC = dyn_cast<BitCastInst>(V))
    V = BC->getOperand(0);

  // TODO: check if it is a constant array
  return false;
}

static bool alwaysSafe(Function &F, unsigned int ArgNo,
                       vector<Module *> &Modules) {

  for (auto *CI : CallSites[makeFnHash(&F)]) {
    Value *Arg = CI->getArgOperand(ArgNo);
    if (isHeapAddress(Arg)) {
      if (isSafeConstArray(F, Arg))
        continue;
      return false;
    }
  }

  return true;
}

void findSafeFunctionArguments(vector<Module *> Modules,
                               vector<PatternBase *> &Patterns) {
  initialize(Modules);
  for (auto *M : Modules) {
    for (auto &F : *M) {
      if (F.isDeclaration() || ICallFuncs.count(makeFnHash(&F)) ||
          /* it is possibile because of inline */
          CallSites.count(makeFnHash(&F)) == 0)
        continue;
      if (isStdFunction(F.getName()))
        continue;
      for (unsigned int i = 0; i < F.arg_size(); i++) {
        if (F.getArg(i)->getType()->isPointerTy()) {
          if (alwaysSafe(F, i, Modules))
            Patterns.push_back(new ValuePattern(new FunArgIdent(
                F.getName().str(), i,
                F.hasLocalLinkage() ? getFileName(M->getModuleIdentifier())
                                    : "")));
        }
      }
    }
  }
}
