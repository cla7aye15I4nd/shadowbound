// from named_r
#include <cstdio>
#include <vector>
#include <malloc.h>
template <class Type>
class ObjectArena
{
public:
  ObjectArena(void) : blockSize(1024), pos(0), end(0){};
  ~ObjectArena(void)
  {
    int i;
    for (i = 0; i < blocks.size(); ++i)
      delete[] blocks[i];
  }

  __attribute__((noinline)) Type *getNewArray(int n)
  {
    Type *rpos;
    rpos = pos;
    if ((pos += n) > end)
    {
      pos = new Type[blockSize];
      printf("malloc size: %lu\n", malloc_usable_size(pos) / sizeof(Type));
      push();
      end = pos + blockSize;
      rpos = pos;
      pos += n;
    }

    return rpos;
  }

  __attribute__((noinline)) void push()
  {
    blocks.push_back(pos);
  }

private:
  int blockSize;
  std::vector<Type *> blocks;
  Type *pos, *end;
};

int main()
{
  ObjectArena<int> arena;

  printf("%p\n", arena.getNewArray(500));
  printf("%p\n", arena.getNewArray(500));
  printf("%p\n", arena.getNewArray(500));
}
