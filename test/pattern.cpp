#include <iostream>

struct Base
{
  virtual int foo()
  {
    return 0;
  }
};

struct Test : public Base
{
  int a, b, c;
  virtual int foo()
  {
    return 1;
  }
};

class Struct
{
public:
  Test *ptr;
  int len;
};

__attribute__((noinline)) void create(Struct *s)
{
  s->ptr = new Test[s->len];
}

int main()
{
  Struct *s = new Struct;
  create(s);
  for (int i = 0; i < s->len; ++i)
  {
    s->ptr[i].a = i;
    s->ptr[i].b = i + 1;
    s->ptr[i].c = i + 2;
  }

  int sum = 0;
  for (int i = 0; i < s->len; ++i)
  {
    if (s->ptr[i].a % 2 == 1)
      sum += s->ptr[i].b;
    else
      sum += s->ptr[i].c;
  }

  std::cout << sum << "\n";
  return 0;
}