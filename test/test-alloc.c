#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

struct chunk
{
  struct chunk *next;
  char *data;
};

int main()
{
  struct chunk *head = malloc(sizeof(struct chunk));
  for (int i = 0; i < 0x1000; ++i)
  {
    struct chunk *c = malloc(sizeof(struct chunk));
    c->next = head;

    if (rand() & 1) {
      c->data = malloc(rand() % 1024);
      if (c->data > (char *)0x700000000000ULL || c->data < (char *)0x600000000000ULL)
        printf("ERROR: (%d) 0x%lx\n", i, (unsigned long)c->data);
    } else {
      c->data = malloc(rand() % 1024 + 0x1002000);
      if (c->data > (char *)0x700000000000ULL || c->data < (char *)0x600000000000ULL)
        printf("ERROR: (%d) 0x%lx\n", i, (unsigned long)c->data);
    }
    head = c;
  }

  return 0;
}