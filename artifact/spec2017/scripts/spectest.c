#include <stdio.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

struct rusage rusage;
struct timeval start, end;
pid_t pid;

static inline int run(const char *file, char *const *argv)
{
  pid_t pid = fork();
  if (pid == 0)
  {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    execvp(file, argv);
    exit(-1);
  }
  else if (pid > 0)
  {
    int status;
    wait4(pid, &status, 0, &rusage);
    gettimeofday(&end, NULL);
    return status;
  }
  else
  {
    perror("fork");
    return 1;
  }
}

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    printf("Usage: exe_file args\n");
    printf("e.g: perlbench_s -I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1\n");
    return 1;
  }

  int time_used, cpu_time_used, memory_used, signum;

  gettimeofday(&start, NULL);

  int ret = run(argv[1], (char *const *)&argv[1]);
  signum = WEXITSTATUS(ret);

  time_used = (int)(end.tv_sec * 1000 + end.tv_usec / 1000 - start.tv_sec * 1000 -
                    start.tv_usec / 1000);
  cpu_time_used =
      rusage.ru_utime.tv_sec * 1000 + rusage.ru_utime.tv_usec / 1000 +
      rusage.ru_stime.tv_sec * 1000 + rusage.ru_stime.tv_usec / 1000;

  memory_used = rusage.ru_maxrss;

  printf("time:     %6d ms\n", time_used);
  printf("memory:   %6d kb\n", memory_used);
  printf("exit:     %6d\n", signum);
  fflush(stdout);

  return 0;
}