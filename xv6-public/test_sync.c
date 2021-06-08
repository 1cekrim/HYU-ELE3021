#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

int
main(int argc, char* argv[])
{
    char path[] = "testsync0";
    char data[512];

    printf(1, "testsync starting\n");
    memset(data, 'a', sizeof(data));

    for (int i = 0; i < 4; i++)
    {
        printf(1, "write %d\n", i);
        path[8] += 1;
        int fd = open(path, O_CREATE | O_RDWR);

        for (int j = 0; j < 20; j++)
        {
            write(fd, data, sizeof(data));
            printf(1, "log_num: %d (%d, %d)\n", get_log_num(), i, j);
        }
        sync();
        // printf(1, "log_num: %d\n", get_log_num());

        close(fd);
        printf(1, "%d\n", get_log_num());
    }

    exit();
}