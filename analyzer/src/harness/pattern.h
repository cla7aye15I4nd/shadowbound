#ifndef PATTERN_H
#define PATTERN_H

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/Identification.h"

#include <string>
#include <vector>

llvm::DataLayout *getDataLayout(llvm::Module *M);
llvm::DominatorTree *getDominatorTree(llvm::Function *F);
llvm::PostDominatorTree *getPostDominatorTree(llvm::Function *F);

void findSafeFunctionArguments(std::vector<llvm::Module *> Modules,
                               std::vector<llvm::PatternBase *> &Patterns);
void findSafeStructMembers(std::vector<llvm::Module *> Modules,
                           std::vector<llvm::PatternBase *> &Patterns);
void dumpPatternOptFile(std::string Filename,
                        std::vector<llvm::Module *> &Modules);

#endif