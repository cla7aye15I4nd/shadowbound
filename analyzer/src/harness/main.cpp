#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>

using namespace llvm;

static cl::list<std::string> InputFilenames(cl::Positional,
                                            cl::desc("<input bitcode files>"),
                                            cl::OneOrMore);

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv);

  LLVMContext Context;
  SMDiagnostic Err;
  for (auto &InputFilename : InputFilenames) {
    std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
    if (!M) {
      Err.print(argv[0], errs());
      return 1;
    }
  }

  return 0;
}