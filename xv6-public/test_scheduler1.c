/**
 * This program runs various workloads cuncurrently.
 */


#include "types.h"
#include "stat.h"
#include "user.h"

#define LIFETIME		(1000)	/* (ticks) */
#define COUNT_PERIOD	(1000000)	/* (iteration) */

#define MLFQ_LEVEL		(3)	/* Number of level(priority) of MLFQ scheduler */

#define WORKLOAD_NUM	(7) /* The number of workloads */

/**
 * This function requests portion of CPU resources with given parameter
 * value by calling set_cpu_share() system call.
 * It reports the cnt value which have been accumulated during LIFETIME.
 */
void
test_stride(int portion, int pipe)
{
	int cnt = 0;
	int i = 0;
	int start_tick;
	int curr_tick;

	if (set_cpu_share(portion) != 0) {
		printf(1, "FAIL : set_cpu_share\n");
		return;
	}

	/* Get start tick */
	start_tick = uptime();

	for (;;) {
		i++;
		if (i >= COUNT_PERIOD) {
			cnt++;
			i = 0;

			/* Get current tick */
			curr_tick = uptime();

			if (curr_tick - start_tick > LIFETIME) {
				/* Time to terminate */
				break;
			}
		}
	}

	/* Report */
	printf(1, "STRIDE(%d%%), cnt : %d\n", portion, cnt);
	printf(pipe, "%d\n", cnt);

	return;
}

/**
 * This function request to make this process scheduled in MLFQ. 
 * MLFQ_NONE			: report only the cnt value
 * MLFQ_LEVCNT			: report the cnt values about each level
 * MLFQ_YIELD			: yield itself, report only the cnt value
 * MLFQ_YIELD_LEVCNT	: yield itself, report the cnt values about each level
 */
enum { MLFQ_NONE, MLFQ_LEVCNT, MLFQ_YIELD, MLFQ_LEVCNT_YIELD };
void
test_mlfq(int type, int pipe)
{
	int cnt_level[MLFQ_LEVEL] = {0, 0, 0};
	int cnt = 0;
	int i = 0;
	int curr_mlfq_level;
	int start_tick;
	int curr_tick;

	/* Get start tick */
	start_tick = uptime();

	for (;;) {
		i++;
		if (i >= COUNT_PERIOD) {
			cnt++;
			i = 0;

			if (type == MLFQ_LEVCNT || type == MLFQ_LEVCNT_YIELD ) {
				/* Count per level */
				curr_mlfq_level = getlev(); /* getlev : system call */
				cnt_level[curr_mlfq_level]++;
			}

			/* Get current tick */
			curr_tick = uptime();

			if (curr_tick - start_tick > LIFETIME) {
				/* Time to terminate */
				break;
			}

			if (type == MLFQ_YIELD || type == MLFQ_LEVCNT_YIELD) {
				/* Yield process itself, not by timer interrupt */
				yield();
			}
		}
	}

	/* Report */
	if (type == MLFQ_LEVCNT || type == MLFQ_LEVCNT_YIELD ) {
		printf(1, "MLfQ(%s), cnt : %d, lev[0] : %d, lev[1] : %d, lev[2] : %d\n",
				type == MLFQ_LEVCNT ? "compute" : "yield", cnt, cnt_level[0], cnt_level[1], cnt_level[2]);
	} else {
		printf(1, "MLfQ(%s), cnt : %d\n",
				type == MLFQ_NONE ? "compute" : "yield", cnt);
	}

	printf(pipe, "%d", cnt);

	return;
}

struct workload {
	void (*func)(int, int);
	int arg;
};

int
main(int argc, char *argv[])
{
	int pid;
	int i;
	/* Workload list */
	struct workload workloads[WORKLOAD_NUM] = {
		/* Process scheduled by Stride scheduler, use 5% of CPU resources */
		// {test_stride, 1},
		// /* Process scheduled by Stride scheduler, use 15% of CPU resources */
		// {test_stride, 2},
		// {test_stride, 3},
		// {test_stride, 4},
		// {test_stride, 5},
		// {test_stride, 6},
		// {test_stride, 7},
		// {test_stride, 8},
		// {test_stride, 9},
		// {test_stride, 10},
		{test_stride, 50},
		{test_stride, 25},
		{test_stride, 5},
		// {test_stride, 15},
		{test_mlfq, MLFQ_NONE},
		{test_mlfq, MLFQ_NONE},
		{test_mlfq, MLFQ_NONE},
		{test_mlfq, MLFQ_NONE},
		// {test_mlfq, MLFQ_NONE},
		// {test_mlfq, MLFQ_NONE},
		// {test_mlfq, MLFQ_NONE},
		// {test_mlfq, MLFQ_NONE},
		// {test_mlfq, MLFQ_NONE},

		// {test_mlfq, MLFQ_NONE},
	};

	int pipes[WORKLOAD_NUM][2];
	for (i = 0; i < WORKLOAD_NUM; ++i)
	{
		if (pipe(pipes[i]) < 0)
		{
			printf(1, "pipe failure");
			exit();
		}
	}

	int results[WORKLOAD_NUM];

	for (i = 0; i < WORKLOAD_NUM; i++) {
		pid = fork();
		if (pid > 0) {
			close(pipes[i][1]);
			/* Parent */
			continue;
		} else if (pid == 0) {
			/* Child */
			void (*func)(int, int) = workloads[i].func;
			int arg = workloads[i].arg;
			/* Do this workload */
			close(pipes[i][0]);
			func(arg, pipes[i][1]);
			exit();
		} else {
			printf(1, "FAIL : fork\n");
			exit();
		}
	}

	for (i = 0; i < WORKLOAD_NUM; i++) {
		wait();
	}

	int total = 0;
	for (i = 0; i < WORKLOAD_NUM; ++i)
	{
		char buf[1000];
		read(pipes[i][0], buf, 999);
		results[i] = atoi(buf);
		total += results[i];
	}

	printf(1, "\n --- test_scheduler result --- \n");
	printf(1, "total: %d\n", total);

	int stridetickets = 0;
	for (i = 0; i < WORKLOAD_NUM; ++i)
	{
		if (workloads[i].func == test_stride)
		{
			stridetickets += workloads[i].arg;
		}
	}

	int mlfqcount = 0;
	for (i = 0; i < WORKLOAD_NUM; ++i)
	{
		if (workloads[i].func == test_mlfq)
		{
			++mlfqcount;
		}
	}

	int mlfqpercent = mlfqcount ? (100 - stridetickets) / mlfqcount : 0;

	for (i = 0; i < WORKLOAD_NUM; ++i)
	{
		float percent = (float)results[i] * 100 / total;
		int digit = (int)percent;
		int decimal = (int)(percent * 10) - (digit * 10);
		int isstride = workloads[i].func == test_stride;
		printf(1, " %d. %s(%d%%) : %d.%d%%\n", i, (isstride ? "STRIDE" : "  MLFQ"), (isstride ? workloads[i].arg : mlfqpercent), digit, decimal);
	}

	if (mlfqpercent)
	{
		int mlfqtotalusage = 0;
		for (i = 0; i < WORKLOAD_NUM; ++i)
		{
			if (workloads[i].func == test_mlfq)
			{
				mlfqtotalusage += results[i];
			}
		}
		float percent = (float)mlfqtotalusage * 100 / total;
		int digit = (int)percent;
		int decimal = (int)(percent * 10) - (digit * 10);
		
		printf(1, " MLFQ TOTAL(%d%%) : %d.%d%%\n", 100 - stridetickets, digit, decimal);
	}

	exit();
}