#ifndef PATTERN_H
#define PATTERN_H

#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/Identification.h>

#include <string>
#include <vector>

void dumpPatternOptFile(std::string Filename, std::vector<llvm::Module *> &Modules);

#endif