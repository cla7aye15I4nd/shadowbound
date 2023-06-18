#include "pattern.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>

#include <vector>

using namespace llvm;
using namespace std;

static cl::list<std::string> InputFilenames(cl::Positional,
                                            cl::desc("<input bitcode files>"),
                                            cl::OneOrMore);

static cl::opt<std::string> PatternOptFile("pattern-opt-file",
                                           cl::desc("Specify the pattern file"),
                                           cl::value_desc("filename"),
                                           cl::init(""));

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv);

  LLVMContext Context;
  SMDiagnostic Err;

  vector<Module *> Modules;
  for (auto &InputFilename : InputFilenames) {
    std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
    if (!M) {
      Err.print(argv[0], errs());
      return 1;
    }

    Modules.push_back(M.release());
  }

  if (PatternOptFile != "")
    dumpPatternOptFile(PatternOptFile, Modules);

  return 0;
}