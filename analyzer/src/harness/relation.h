#ifndef RELATION_H_
#define RELATION_H_

#include <string>
using namespace std;

enum IdentType {
  IT_SIMPLE_GLOBAL_POINTER,
  IT_SIMPLE_STRUCT_MEMBER,
};

class IdentDesc {
  IdentType itype;

public:
  IdentDesc(IdentType itype) : itype(itype) {}
};

class SimpleGlobalPointer : public IdentDesc {
  string name;

public:
  SimpleGlobalPointer(string name) : IdentDesc(IT_SIMPLE_GLOBAL_POINTER), name(name) {}
};

class SimpleStructMember : public IdentDesc {
  string structName;
  string memberName;
  int memberIndex;

public:
  SimpleStructMember(string structName, string memberName, int memberIndex)
      : IdentDesc(IT_SIMPLE_STRUCT_MEMBER), structName(structName), memberName(memberName), memberIndex(memberIndex) {}
};

#endif /* RELATION_H_ */