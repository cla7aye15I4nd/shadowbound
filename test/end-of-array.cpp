// from named_r
#include <cstdio>
#include <vector>

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

  printf("%p\n", arena.getNewArray(400));
  printf("%p\n", arena.getNewArray(400));
  printf("%p\n", arena.getNewArray(400));
}
