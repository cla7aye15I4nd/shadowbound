#include "pattern.h"

#include "llvm/Support/Debug.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <set>
#include <string>

using namespace std;
using namespace llvm;

static bool alwaysNonHeap(Function &F, unsigned int ArgNo,
                          vector<Module *> &Modules) {
  return true;
}

static void findNonHeapFunctionArguments(vector<Module *> Modules,
                                         vector<PatternBase *> &Patterns) {

  for (auto *M : Modules) {
    for (auto &F : *M) {
      if (F.isDeclaration())
        continue;
      for (unsigned int i = 0; i < F.arg_size(); i++) {
        if (alwaysNonHeap(F, i, Modules)) {
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