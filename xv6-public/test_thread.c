#include "types.h"
#include "stat.h"
#include "user.h"

int
func(int a)
{
  while (1)
  {
    printf(1, "thread %d\n", a);
  }

  return a;
}

int
main()
{
  thread_t thread;
  int arg = 1;
  int ret = thread_create(&thread, (void*)func, &arg);
  printf(1, "ret = %d\n", ret);
  while (1)
  {
  }
  exit();
}