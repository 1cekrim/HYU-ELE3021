#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *arbgv[])
{
    printf(1, "My pid is %d\n", getpid());
    exit();
}