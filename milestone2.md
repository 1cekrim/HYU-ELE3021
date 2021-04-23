# Milestone 2

## 목차

1. [목차](#목차)
2. [MLFQ](#mlfq)  
    1. [queue 구현](#queue-구현)
    2. [MLFQ 구현](#mlfq-구현)
        1. [mlfqtop](#mlfqtop)
        2. [mlfqnext](#mlfqnext)
        3. [mlfqboost](#mlfqboost)
3. [Stride](#stride)
4. [MLFQ/Stride](#mlfq/stride)
5. [테스트 프로그램](#테스트-프로그램)

## MLFQ

```c
// scheduler.h
void mlfqinit();
int mlfqpush(struct proc*);
struct proc* mlfqtop();
int mlfqnext(struct proc*, uint, uint);
void mlfqboost();
```

> ```c
> void mlfqinit()
> ```
>
> 전역 MLFQ 객체를 초기화합니다.
---

> ```c
> int mlfqpush(struct proc* p)
> ```
>
> `p` 를 MLFQ의 highest priority queue에 넣습니다.
---

> ```c
> struct proc* mlfqtop()
> ```
>
> MLFQ에 있는 RUNNABLE한 `struct proc*`중, 현재 가장 우선순위가 높은 것을 반환합니다.
---

> ```c
> int mlfqnext(struct proc* p, uint start, uint end)
> ```
>
> `struct proc* p`  
> 마지막으로 `mlfqtop`을 통해 얻은 `struct proc*`  
>
> `uint start`  
> 프로세스의 context로 swtch되기 직전에 측정한 `sys_uptime`  
>
> `uint end`  
> 프로세스가 scheduler의 context로 swtch한 직후에 측정한 `sys_uptime`  
>
> `p`, `start`, `end` 값들을 기반으로 MLFQ를 다음 상태로 전이시킵니다
---

> ```c
> void mlfqboost()
> ```
>
> priority boosting을 진행합니다.

### queue 구현

MLFQ에서 사용하기 위해 queue를 구현했습니다

```c
struct mlfqueue
{
  struct proc* q[MSIZE];
  int rear;
  int front;
  int qsize;
  int capacity;
};
```

전체 OS 내에서 최대 NPROC개의 프로세스가 존재할 수 있으므로, queue에 최대 NPROC개를 넣을 수 있도록 했습니다.  
RR를 진행한다는 mlfq의 특성상 push와 pop이 자주 일어나므로, circular queue로 구현했습니다.

### MLFQ 구현

```c
struct
{
  int quantum[NLEVEL];
  int allotment[NLEVEL - 1];
  int boostingperiod;
  struct mlfqueue q[NLEVEL];
} mlfq;
```

#### mlfqtop

MLFQ algorithm으로 이번에 실행할 프로세스를 결정하는 것은 `mlfqtop` 함수에서 맡습니다.  
runnable한 프로세스가 나올 때 까지 dequeue, enqueue를 반복합니다.  
만약 현재 level에 runnable한 프로세스가 없다면, 다음 레벨로 넘어갑니다.

```c
struct proc*
mlfqtop()
{
  for (int level = 0; level < NLEVEL; ++level)
  {
    int size = mlfqueuesize(level);

    // 해당 level이 비어 있음
    if (!size)
    {
      continue;
    }

    struct proc* it;

    // q에서 runnable한 프로세스틑 찾음
    for (int i = 0; i < size; ++i)
    {
      it = mlfqueuetop(level);
      assert(!it, "invalid top");

      if (it->state == RUNNABLE)
      {
        return it;
      }

      // round robin
      assert(mlfqdequeue(level) == QFAILURE, "mlfqdequeue failure");
      assert(mlfqenqueue(level, it) == QFAILURE, "mlfqenqueue failure");
    }
  }

  return 0;
}
```

#### mlfqnext

프로세스의 실행이 끝나고 scheduler로 swtch되면, `mlfqnext` 함수가 호출됩니다.  
`mlfqnext` 함수는 현재 선택된 프로세스의 실행 시간을 구하고, MLFQ algorithm에 따라 MLFQ의 상태를 전이시킵니다.  
실행해야 할 프로세스가 변하지 않았다면 0을 반환하고, 만약 변했다면 1을 반환합니다. scheduler는 `mlfqnext`의 반환값이 1일 때만 `mlfqtop`을 호출해 다음으로 실행할 프로세스를 가져오면 됩니다. 이를 통해 프로세스가 변하지 않았는데도 `mlfqtop`을 호출해 성능에 손해를 보는 상황을 방지할 수 있습니다.  

프로세스는 실행 과정에서 `kill` 당하거나 `exit`를 호출할 수 있습니다. 이 경우 해당 프로세스는 더는 유효하지 않으므로, 바로 1을 반환해 scheduler가 새로운 프로세스를 할당받도록 했습니다.

```c
if (p->killed || p->state == ZOMBIE)
{
  return 1;
}
```

프로세스가 level 0, 1에 위치하고, executionticks가 해당 level의 allotment를 넘었을 경우, 프로세스의 level을 낮춰줘야 합니다.  
일반적으로는 `p`가 queue의 맨 앞에 위치하지만, priority boosting으로 인해 `p`의 우선순위가 변할 경우나 queue에서 다른 프로세스가 remove 되는 경우에는 `p`가 queue의 맨 앞에 위치하지 않을 수도 있게 됩니다. queue 자료구조에서는 맨 앞의 element만 삭제가 가능하므로, `mlfqrotatetotarget` 함수를 이용해 `p`가 queue의 맨 앞에 위치하게 해 줍니다.  
그 다음 dequeue로 해당 level에서 `p`를 지우고, enqueue로 다음 level에 `p`를 넣어주면 됩니다.

```c
int level           = p->schedule.level;
uint executionticks = p->schedule.executionticks;

if (level + 1 < NLEVEL && executionticks >= mlfq.allotment[level])
{
  assert(mlfqrotatetotarget(level, p) == QFAILURE, "rotate failure");
  assert(mlfqdequeue(level) == QFAILURE, "mlfqpop failure");
  assert(mlfqenqueue(level + 1, p) == QFAILURE, "mlfqpush failure");
  return 1;
}
```

프로세스가 cpu를 점유한 시간(`executiontick`)을 이용해 Round Robin을 진행합니다.  
`executiontick`이 quantum의 이상일 때 뿐만 아니라, `p`가 `yield` syscall을 호출했거나, `p`의 상태가 `SLEEPING`인 경우에도 다음 순서로 넘깁니다.

```c
int result = (executiontick >= mlfq.quantum[level]) || p->schedule.yield ||
              p->state == SLEEPING;
if (result)
{
  assert(mlfqrotatetotarget(level, p) == QFAILURE, "rotate failure");
  assert(mlfqdequeue(level) == QFAILURE, "mlfqpop failure");
  assert(mlfqenqueue(level, p) == QFAILURE, "mlfqpush failure");
}
```

여기서 프로세스가 cpu를 점유한 시간(`executiontick`)은 아래와 같이 계산됩니다.  

```c
int executiontick = end - start + ((p->schedule.yield) ? 1 : 0);
```

시간은 연속적이지만, xv6의 tick은 10ms마다 증가하는 불연속적인 값입니다. 즉 `sys_uptime`으로 측정하는 시각은 10ms 밑의 시간을 모두 내림 한 것과 같습니다.  
이로 인해 프로세스가 1tick보다 빠르게 `yield`를 계속 호출한다면, `executionticks`가 항상 0이므로 해당 프로세스의 우선순위는 감소되지 않게 됩니다.  
이를 방지하기 위해, `yield`가 호출되었을 때에만 `executiontick`을 1 증가시켜 줍니다.  
`yield`한 프로세스에게만 위와 같은 패널티를 부과하기 떄문에 `yield`를 호출하는 프로세스는 다른 프로세스에 비해 더 빠르게 우선순위가 감소하게 됩니다.  
하지만 이는 위에서 언급한 `yield` 호출 문제를 해결하기 위해 어쩔 수 없이 발생하는 문제입니다. 오히려 `yield`가 다른 프로세스에게 자원을 양보할 때 사용되는 syscall이라는 것을 고려하면, 이렇게 패널티를 부과하는 것이 non-busy waiting와 같은 일부 상황에서는 효율적으로 작용할 수도 있겠습니다.

위 작업이 모두 끝난 이후, 마지막으로 priority boosting을 진행한 순간부터 일정 시간(100 tick)이 경과했다면 priority boosting을 진행합니다.

```c
if (nextboostingtick <= end)
{
  mlfqboost();
  nextboostingtick = end + mlfq.boostingperiod;
}
```

#### mlfqboost

priority boosting에서는, level 1, 2인 queue에 들어있는 프로세스들을 level 0인 queue로 옮겨줍니다.

```c
void
mlfqboost()
{
  struct proc* p = 0;
  for (int level = 1; level < NLEVEL; ++level)
  {
    int capacity = mlfq.q[level].capacity;
    int front    = mlfq.q[level].front;
    for (int i = 0; i < mlfq.q[level].qsize; ++i)
    {
      front = (front + 1) % capacity;
      p     = mlfq.q[level].q[front];
      mlfqenqueue(0, p);
    }
    mlfq.q[level].front = -1;
    mlfq.q[level].rear  = -1;
    mlfq.q[level].qsize = 0;
  }
}
```

## Stride

strideinit
stridepush
stridetop


## MLFQ/Stride

## 테스트 프로그램

### 테스트 결과

```shell
$ test

[ TEST only mlfq test ]
test_scheduler
t->argv: test_scheduler, 0
MLfQ(compute), cnt : 145276
MLfQ(compute), cnt : 147123
MLfQ(compute), cnt : 147855
MLfQ(compute), cnt : 146704
MLfQ(compute), cnt : 147477
MLfQ(compute), cnt : 147089
MLfQ(compute), cnt : 147233
MLfQ(compute), cnt : 150054
MLfQ(compute), cnt : 152978
MLfQ(compute), cnt : 153866

 --- test_scheduler result --- 
total: 1485655
 0.   MLFQ(10%) : 9.7%
 1.   MLFQ(10%) : 9.9%
 2.   MLFQ(10%) : 9.9%
 3.   MLFQ(10%) : 9.8%
 4.   MLFQ(10%) : 9.9%
 5.   MLFQ(10%) : 9.9%
 6.   MLFQ(10%) : 9.9%
 7.   MLFQ(10%) : 10.1%
 8.   MLFQ(10%) : 10.2%
 9.   MLFQ(10%) : 10.3%
 MLFQ TOTAL(100%) : 100.0%
[ TEST only mlfq test FINISHED ]

[ TEST only stride test ]
test_scheduler
t->argv: test_scheduler, 1
STRIDE(10%), cnt : 146764
STRIDE(10%), cnt : 147262
STRIDE(5%), cnt : 51050
STRIDE(5%), cnt : 73839
STRIDE(5STRIDE(5%), cnt : 74366
STRIDE(5%), cnt : 74097
STRIDE(20%), cnt : 292164
STRIDE(15%), cnt : 220183
MLfQ(compute), cnt : 293499
%), cnt : 62077

 --- test_scheduler result --- 
total: 1435301
 0. STRIDE(5%) : 5.1%
 1. STRIDE(5%) : 5.1%
 2. STRIDE(5%) : 3.5%
 3. STRIDE(5%) : 5.1%
 4. STRIDE(5%) : 4.3%
 5. STRIDE(10%) : 10.2%
 6. STRIDE(10%) : 10.2%
 7. STRIDE(15%) : 15.3%
 8. STRIDE(20%) : 20.3%
 9.   MLFQ(20%) : 20.4%
 MLFQ TOTAL(20%) : 20.4%
[ TEST only stride test FINISHED ]

[ TEST 50 : 50 test ]
test_scheduler
t->argv: test_scheduler, 2
STRIDE(5%), cnt : 73727
STRIDE(5%), cnt : 68604
STRIDE(20%), cnt : 292955
STRIDE(10%), cnt : 147781
STRIDE(10%), cnt : 148368
MLfQ(compute), cnt : 145440
MLfQ(compute), cnt : 147507
MLfQ(compute), cnt : 144977
MLfQ(compute), cnt : 111242
MLfQ(compute), cnt : 173828

 --- test_scheduler result --- 
total: 1454429
 0. STRIDE(5%) : 5.0%
 1. STRIDE(5%) : 4.7%
 2. STRIDE(10%) : 10.1%
 3. STRIDE(10%) : 10.2%
 4. STRIDE(20%) : 20.1%
 5.   MLFQ(10%) : 9.9%
 6.   MLFQ(10%) : 10.1%
 7.   MLFQ(10%) : 9.9%
 8.   MLFQ(10%) : 7.6%
 9.   MLFQ(10%) : 11.9%
 MLFQ TOTAL(50%) : 49.7%
[ TEST 50 : 50 test FINISHED ]

[ TEST 80 : 20 test ]
test_scheduler
t->argv: test_scheduler, 3
STRIDE(10%), cnt : 147761
STRIDE(15%), cnt : 219675
STRIDE(5%), cnt : 74237
STRIDE(45%), cnt : 657126
STRIDE(5%), cnt : 73154
MLfQ(compute), cnt : 58028
MLfQ(compute), cnt : 64097
MLfQ(compute), cnt : 65835
MLfQ(compute), cnt : 66820
MLfQ(compute), cnt : 73414

 --- test_scheduler result --- 
total: 1500147
 0. STRIDE(5%) : 4.8%
 1. STRIDE(5%) : 4.9%
 2. STRIDE(10%) : 9.8%
 3. STRIDE(15%) : 14.6%
 4. STRIDE(45%) : 43.8%
 5.   MLFQ(4%) : 3.8%
 6.   MLFQ(4%) : 4.2%
 7.   MLFQ(4%) : 4.3%
 8.   MLFQ(4%) : 4.4%
 9.   MLFQ(4%) : 4.8%
 MLFQ TOTAL(20%) : 21.8%
[ TEST 80 : 20 test FINISHED ]
// test master end //
```

### master test

```c
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
```

위처럼 X 매크로를 이용해 `struct test` 배열을 생성합니다.

```c
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
```

`tests`를 순회하며 테스트 프로그램을 실행시킵니다.

위와 같은 방법들을 이용해 여러 개의 테스트를 한번에 진행할 수 있도록 했습니다.

### mlfq/stride test