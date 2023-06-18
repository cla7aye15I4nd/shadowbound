#ifndef LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H
#define LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"

#include <string>
#include <vector>

#define ASSERT(X)                                                              \
  do {                                                                         \
    if (!(X)) {                                                                \
      printf("Assertion failed at Identification.h: " #X "\n");                \
      abort();                                                                 \
    }                                                                          \
  } while (0)

namespace llvm {

enum ValueIdentType {
  VIT_GLOBAL,
  VIT_STRUCT,
  VIT_FUNARG,
  VIT_UNKNOWN,
};

class ValueIdentBase {
  ValueIdentType Type;

public:
  ValueIdentBase(ValueIdentType Type) : Type(Type) {}
  ValueIdentType getType() const { return Type; }

  virtual void print(raw_ostream &OS) const { ASSERT(0); }
  friend raw_ostream &operator<<(raw_ostream &OS, const ValueIdentBase &VI) {
    VI.print(OS);
    return OS;
  }
};

class GlobalIdent : public ValueIdentBase {
  std::string Name;

public:
  GlobalIdent(std::string Name) : ValueIdentBase(VIT_GLOBAL), Name(Name) {}
  std::string getName() const { return Name; }

  void print(raw_ostream &OS) const override { OS << "Global(" << Name << ")"; }
};

class StructMemberIdent : public ValueIdentBase {
  std::string Name;
  unsigned int Index;

public:
  StructMemberIdent(std::string Name, unsigned int Index)
      : ValueIdentBase(VIT_STRUCT), Name(Name), Index(Index) {}

  std::string getName() const { return Name; }
  unsigned int getIndex() const { return Index; }

  void print(raw_ostream &OS) const override {
    OS << "StructMember(" << Name << ", " << Index << ")";
  }
};

class FunArgIdent : public ValueIdentBase {
  std::string Name;
  unsigned int Index;

public:
  FunArgIdent(std::string Name, unsigned int Index)
      : ValueIdentBase(VIT_FUNARG), Name(Name), Index(Index) {}

  std::string getName() const { return Name; }
  unsigned int getIndex() const { return Index; }

  void print(raw_ostream &OS) const override {
    OS << "FunArg(" << Name << ", " << Index << ")";
  }
};

class UnknownIdent : public ValueIdentBase {
public:
  UnknownIdent() : ValueIdentBase(VIT_UNKNOWN) {}

  void print(raw_ostream &OS) const override { OS << "Unknown()"; }
};

enum PatternType {
  PT_VALUE,
  PT_ARRAY,
};

class PatternBase {
  PatternType Type;

public:
  PatternBase(PatternType Type) : Type(Type) {}
  PatternType getType() const { return Type; }

  virtual void print(raw_ostream &OS) const { ASSERT(0); }
  friend raw_ostream &operator<<(raw_ostream &OS, const PatternBase &P) {
    P.print(OS);
    return OS;
  }
};

class ValuePattern : public PatternBase {
  ValueIdentBase Ident;

public:
  ValuePattern(ValueIdentBase Ident) : PatternBase(PT_VALUE), Ident(Ident) {}
  ValueIdentBase getIdent() const { return Ident; }

  void print(raw_ostream &OS) const override {
    OS << "ValuePattern[" << Ident << "]";
  }
};

class ArrayPatternBase : public PatternBase {
  ValueIdentBase Pointer;
  ValueIdentBase Length;
  int Slope;
  int Intercept;

public:
  ArrayPatternBase(PatternType Type, ValueIdentBase Pointer,
                   ValueIdentBase Length, int Slope, int Intercept)
      : PatternBase(Type), Pointer(Pointer), Length(Length), Slope(Slope),
        Intercept(Intercept) {}
  ValueIdentBase getPointer() const { return Pointer; }
  ValueIdentBase getLength() const { return Length; }
  int getSlope() const { return Slope; }
  int getIntercept() const { return Intercept; }

  void print(raw_ostream &OS) const override {
    OS << "ArrayPattern[ptr=" << Pointer << ", len=" << Length
       << ", k=" << Slope << ", b=" << Intercept << "]";
  }
};

std::vector<PatternBase *> parsePatternFile(std::string Filename);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_IDENTIFICATION_H