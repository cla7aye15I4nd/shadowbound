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

DominatorTree *getDominatorTree(Function *F) {
  static DenseMap<Function *, DominatorTree *> DomTrees;
  if (DomTrees.find(F) == DomTrees.end()) {
    DominatorTree *DT = new DominatorTree(*F);
    DT->recalculate(*F);
    DomTrees[F] = DT;
  }

  return DomTrees[F];
}

PostDominatorTree *getPostDominatorTree(Function *F) {
  static DenseMap<Function *, PostDominatorTree *> PostDomTrees;
  if (PostDomTrees.find(F) == PostDomTrees.end()) {
    PostDominatorTree *PDT = new PostDominatorTree(*F);
    PDT->recalculate(*F);
    PostDomTrees[F] = PDT;
  }

  return PostDomTrees[F];
}

DataLayout *getDataLayout(Module *M) {
  static DenseMap<Module *, DataLayout *> DataLayouts;
  if (DataLayouts.find(M) == DataLayouts.end()) {
    DataLayouts[M] = new DataLayout(M);
  }

  return DataLayouts[M];
}

static void printPatternOptFile(string Filename,
                                vector<PatternBase *> &Patterns) {
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

void dumpPatternOptFile(string Filename, vector<Module *> &Modules) {
  vector<PatternBase *> Patterns;

  findSafeFunctionArguments(Modules, Patterns);
  findSafeStructMembers(Modules, Patterns);

  printPatternOptFile(Filename, Patterns);
}