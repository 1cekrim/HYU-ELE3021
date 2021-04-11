#include "types.h"
#include "stat.h"
#include "user.h"

#define TESTS                                   \
X("test mlfqueue", "test_mlfqueue", "argv1", "argv2")   \
X("test mlfqueue2", "test_mlfqueue", "argv1", "argv2")  \
X("test mlfqueue3", "test_mlfqueue", "argv1", "argv2")  \


struct test
{
    char* name;
    char* path;
    char** argv;
};

struct test tests[] =
{
#define X(name, path, ...) {(name), (path), (char *[]){ __VA_ARGS__ }},
    TESTS
#undef X
};

int main(int argc, char *arbgv[])
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
            continue;
        }
        else
        {
            exec(t->path, tests->argv);
            printf(1, "! EXEC FAIL %s !\n", t->name);
        }
    }

    printf(1, "test master end\n");
    exit();
}