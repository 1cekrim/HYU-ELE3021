#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *arbgv[])
{
    int pid = fork();

    if (pid)
    {
        while(1)
        {
            printf(1, "Parent\n");
            yield();
        }
    }
    else
    {
        while (1)
        {
            printf(1, "Child\n");
            yield();
        }
    }

    return 0;
}