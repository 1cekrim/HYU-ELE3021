#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define DATASIZE 100

int
main(int argc, char* argv[])
{
  int data[DATASIZE];
  for (int i = 0; i < DATASIZE; ++i)
  {
    data[i] = i;
  }

  int fd = open("pwrtest", O_CREATE | O_RDWR);
  write(fd, &data, sizeof(data));

  for (int i = DATASIZE - 1; i >= 0; --i)
  {
    int a;
    pread(fd, &a, sizeof(a), i * sizeof(int));
    printf(1, "%d ", a);
    if (i % 10 == 0)
        printf(1, "\n");
    if (i != a)
    {
      printf(1, "invalid read: %d != %d\n", i, a);
      exit();
    }
  }
  printf(1, "\npread test success\n");

  for (int i = 0; i < DATASIZE; i += 3)
  {
    int a = -1;
    pwrite(fd, &a, sizeof(a), i * sizeof(int));
  }

  for (int i = 0; i < DATASIZE; ++i)
  {
    int a;
    pread(fd, &a, sizeof(a), i * sizeof(int));
    printf(1, "%d ", a);
    if ((i + 1) % 10 == 0)
        printf(1, "\n");
    if (i % 3 == 0 && a != -1)
    {
      printf(1, "invalid read: %d != %d\n", i, a);
      exit();
    }
    else if (i % 3 != 0 && a != i)
    {
      printf(1, "invalid read: %d != %d\n", i, a);
      exit();
    }
  }
  printf(1, "\npwrite test success\n");

  close(fd);
  unlink("pwrtest");

  fd = open("pwrtest", O_CREATE | O_RDWR);
  int c = -1;
  if (write(fd, &c, sizeof(c)) == -1)
  {
    printf(1, "write failure\n");
    exit();
  }
  c = -2;
  if (pwrite(fd, &c, sizeof(c), (DATASIZE - 1) * sizeof(int)) == -1)
  {
    printf(1, "pwrite failure\n");
    exit();
  }

  for (int i = 0; i < DATASIZE; ++i)
  {
    int a;
    pread(fd, &a, sizeof(a), i * sizeof(int));
    printf(1, "%d ", a);
    if ((i + 1) % 10 == 0)
        printf(1, "\n");
    if (i == 0 && a != -1)
    {
      printf(1, "invalid read(%d): %d != %d\n", i, -1, a);
      exit();
    }
    else if (i == DATASIZE - 1 && a != -2)
    {
      printf(1, "invalid read(%d): %d != %d\n", i, -2, a);
      exit();
    }
    else if (i > 0 && i < DATASIZE - 1 && a != 0)
    {
      printf(1, "invalid read(%d): %d != %d\n", i, 0, a);
      exit();
    }
  }
  printf(1, "\npwrite test2 success\n");

  close(fd);
  unlink("pwrtest");

  exit();
}