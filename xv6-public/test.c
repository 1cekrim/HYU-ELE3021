#include "types.h"
#include "stat.h"
#include "user.h"

#define TESTS                                     \
  X("only mlfq test", "test_scheduler", "0", 0)   \
  X("only stride test", "test_scheduler", "1", 0) \
  X("50 : 50 test", "test_scheduler", "2", 0)     \
  X("80 : 20 test", "test_scheduler", "3", 0)

struct test
{
  char* name;
  char* path;
  char** argv;
};

struct test tests[] = {
#define X(name, path, ...) { (name), (path), (char*[]) { path, __VA_ARGS__ } },
  TESTS
#undef X
};

int
main(int argc, char* arbgv[])
{
  int test_cnt = sizeof(tests) / sizeof(struct test);

  for (int test_idx = 0; test_idx < test_cnt; ++test_idx)
  {
    struct test* t = &tests[test_idx];
    printf(1, "\n[ TEST %s ]\n%s\n", t->name, t->path);
    int pid = fork();
    if (pid)
    {
      wait();
      printf(1, "[ TEST %s FINISHED ]\n", t->name);
      continue;
    }
    else
    {
      printf(1, "t->argv: %s, %s\n", t->argv[0], t->argv[1]);
      exec(t->path, t->argv);
      printf(1, "! EXEC FAIL %s !\n", t->name);
    }
  }

  printf(1, "// test master end //\n");
  exit();
}