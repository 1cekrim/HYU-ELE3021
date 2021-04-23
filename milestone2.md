# Milestone 2

## 목차

1. [목차](#목차)
2. [MLFQ](#mlfq)  
    1. [queue 구현](#queue-구현)
    2. [MLFQ 구현](#mlfq-구현)
3. [Stride](#stride)
4. [MLFQ + Stride](#mlfq-+-stride)
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
> `p`, `start`, `end`를 기반으로 MLFQ의 상태를 변경합니다
---

> ```c
> void mlfqboost()
> ```
>
> priority boosting을 진행합니다.

### queue 구현

MLFQ에서 사용되는 queue를 구현했습니다.

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

## Stride

## MLFQ + Stride

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

위와 같은 방법들을 이용해 여러 개의 테스트를 동시에 진행할 수 있도록 했습니다.
