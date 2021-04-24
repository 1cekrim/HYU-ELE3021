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
    1. [strideinit](#strideinit)
    2. [stridepush](#stridepush)
        1. [pass value](#pass-value)
    3. [stridetop](#stridetop)
4. [MLFQ/Stride](#mlfq/stride)
    1. [scheduler](#scheduler)
    2. [allocproc](#allocproc)
    3. [schedremoveproc](#schedremoveproc)
    4. [set_cpu_share](#set_cpu_share)
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
> `p`, `start`, `end` 값들을 기반으로 MLFQ를 다음 상태로 전이시킵니다.  
> `mlfqtop`의 반환값이 이전 상태와 달라졌다면 1을 반환하고, 그대로일 경우 0을 반환합니다.
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

```c
struct pqelement
{
  double key;
  void* value;
  int usage;
};

struct priorityqueue
{
  struct pqelement data[PQCAPACITY];
  int size;
  int capacity;
};

struct stridescheduler
{
  struct priorityqueue pq;
  int totalusage;
  int maxticket;
  int minusage;
  double stride[STRIDEMAXTICKET + 1];
  struct spinlock lock;
};
```

Stride scheduler를 구현하기 위해 priority queue를 먼저 구현했습니다.  
priority queue는 `double key`를 기준으로 하는 min-heap으로 구현했습니다.  

```c
// scheduler.h
void strideinit(struct stridescheduler*, int);
int stridepush(struct stridescheduler*, void*, int);
void* stridetop(struct stridescheduler*);
```

> ```c
> void strideinit(struct stridescheduler* ss, int ticket)
> ```
>
> `struct stridescheduler* ss`
> 초기화할 `struct stridescheduler` 객체의 포인터
>
> `int ticket`
> 최대 티켓 개수(`maxticket`)를 초기화할 값
>
> ss가 가리키는 `struct stridescheduler` 객체를 초기화합니다.
---

> ```c
> int stridepush(struct stridescheduler* ss, void* value, int usage)
> ```
>
> `struct stridescheduler* ss`
> push 할 `struct stridescheduler` 객체의 포인터
>
> `void* value`
> push 될 값
>
> `int usage`
> stride에 사용될 usage
>
> ss가 가리키는 `struct stridescheduler` 객체에 value, usage 값을 가진 element를 추가합니다.
---

### strideinit

stride scheduler를 초기화합니다.  

```c
ss->stride[0] = 0;
ss->minusage  = 100;
for (int i = 1; i <= maxticket; ++i)
{
  ss->stride[i] = (double)1 / i;
}
```

`strideinit`에서 각 usage에 대해 stride 값을 미리 계산해서 저장해 놓습니다.  
일반적으로 div 연산이 add 연산보다 훨씬 느리므로, 미리 배열에 저장해 놓고 index로 접근(add 연산)하는 것이 훨씬 효율적이기 때문입니다.  
특히 `double`의 경우 나눗셈이 정수나 `float`보다 훨씬 더 느린 편이니 중요합니다.

### stridepush

stride scheduler에 value, usage를 기반으로 element를 추가합니다.  

#### pass value

> Milestone1) 새로 추가되는 process의 pass value 설정 문제  
> 새로 추가되는 process의 pass value는, 이미 존재하는 process들의 pass value 중 가장 작은 값으로 설정할 예정입니다.  
> 이 경우, pass value를 0으로 설정했을 때 발생하는 독점 문제가 발생하지 않습니다.  
> 단, 새로 추가되는 process를 우선시한다는 점 때문에, starvation이 발생할 수 있기는 합니다.  

Milestone1에서 언급했듯, 처음 추가되는 프로세스의 pass value를 0으로 설정하면 해당 프로세스의 pass value가 다른 pass value만큼 될 때까지 cpu를 독점하는 문제가 발생합니다.  
그래서 pass value 중 가장 작은 값으로 설정하려 했는데, 이 경우 새로 추가되는 process를 우선시한다는 저 때문에 문제가 발생합니다. 이 문제는 아래에 설명되어 있습니다.

> Milestone1) Combine the stride scheduling algorithm with MLFQ  
> Scheduler만 오랜 시간동안 동작했다면 MLFQ pass가 Stride pass보다 매우 커, starvation이 발생하게 됩니다. 이 경우 pass value가 비슷해질 때 까지 MLFQ Scheduler가 CPU의 20% 이상을 점유하지 못할 수도 있습니다.  
> 이 문제점을 해결하기 위해, Stride Scheduler에 process가 push 될 때마다 pass value들을 0으로 초기화 한다면, 취약점이 발생합니다.  
> Stride Scheduler에 process를 계속 push하고, pop하는 것을 반복한다면 계속 pass value가 0으로 초기화 될 것이고, pass value가 같을 때 우선시되는 Scheduler가 CPU를 독점적으로 사용하게 됩니다.  
> 이를 방지하기 위해서는 아래와 같은 조건으로 pass value를 업데이트 하면 됩니다.  
> pass value가 큰 쪽을 BIG, 작은 쪽을 SMALL이라 하겠습니다.  
>
> 1. BIG pass > SMALL pass + BIG Stride 라면 두 pass 모두를 0으로 만든다.
> 2. BIG pass <= SMALL pass + BIG Stride 라면 pass를 바꾸지 않는다.
> 3. pass가 동일하다면 stride가 큰 쪽을 우선시한다

여기서 언급한 것과 같이, 프로세스를 계속 push하고 pop 하는 것을 반복한다면 계속 `pass value`가 `minimum pass value`와 같게 되어, 항상 해당 프로세스만이 선택되게 되는 문제가 발생합니다.  
이 문제를 해결하기 위해서는, 새롭게 추가된 프로세스의 `pass value`를 `minimum pass value + C` 이렇게 설정해야 합니다. (C는 적절한 값)  
이 `C` 값을 적절하게 설정하지 못한다면 다른 문제가 또 발생합니다.

1. `C`값이 너무 작은 경우
`C`값이 너무 작다면, 새롭게 추가된 프로세스가 priority queue의 중간에 삽입되게 됩니다. 이 경우 추가된 프로세스의 뒤에 위치하는 프로세스들 역시 패널티를 받게 됩니다.  
이로 인해 프로세스를 계속 추가하는 것을 반복한다면, `minimum pass value + C` 보다 큰 `pass value`를 가진 프로세스의 순서가 계속 밀리게 되는 문제가 발생합니다.  
이 문제를 해결하기 위해서는 **프로세스 추가로 인한 패널티를 오로지 추가된 프로세스만 받게 해야 합니다.**  
이를 위해서는 새롭게 추가된 프로세스가 항상 priority queue의 맨 뒤에 위치하게 해야 하고, **`minimum pass value + C`가 `maximum pass value`보다 크게**하면 됩니다.

2. `C`값이 지나치게 큰 경우  
반대로 `C`가 지나치게 커 `maximum pass value`보다 `minimum pass value + C`가 지나치게 커진다면, 새롭게 추가된 프로세스의 순서가 너무 밀리게 될 수 있습니다.  
예를 들어 `maximum pass value`가 100, `minimum pass value + C`가 10000였다면, 새롭게 추가된 프로세스는 `100 + stride * n >= 10000`이 되는 `n`만큼 다른 프로세스가 실행된 이후에야 자원을 할당받게 됩니다.  
즉, **`C`는 너무 크면 안됩니다**

즉 C는 `minimum pass value + C >= maximum pass value`를 만족할 만큼 크면서, 너무 크면 안되는 수로 설정되어야 합니다.  
stride scheduler는 xv6 실행 동안 계속 상태가 변하므로, 이 C를 특정 상수로 두는 것은 어렵습니다.  
그래서 stride scheduler의 상태에 따라 이 C가 적절하게 선택되어야 합니다.  
가장 간단한 방법은 `C = maximum pass value - minimum pass value`로 두는 것입니다. 하지만 이 방법에도 문제가 하나 있는데, min-heap에서 maximum value를 구하는 것은 비효율적이라는 점입니다
따라서 maximum pass value 값을 구하는 것을 최대한 피하기 위해, C를 maximum stride 값으로 두기로 했습니다. 증명은 수학적 귀납법으로 할 수 있는데, 그 증명은 아래와 같습니다.  

---

특정 시점에서의 stride scheduler의 상태를 S_n이라 명명한다.  
S_n에서 S_n+1로 상태가 전이될 때, pass value에 stride 값이 더해지는 프로세스를 p_n이라 명명한다.  
`C = maximum stride >= maximum pass value - minimum pass value` 식은 위 명명 규칙에 따라 `C = max_stride(S_n) >= max_pass_value(S_n) - min_pass_value(S_n)`이 된다.  
위 식이 참이라고가정한다. (1)  
이 때 stride scheduling의 정의에 따라 `pass_value(p_n)=minimum pass value`가 성립한다. (2)  
(1)이 참이라 가정하면, (2)에 의해 `max_stride(S_n) >= stride(p_n) >= max_pass_value(S_n) - pass_value(p_n)`이 성립한다. (3)  
n=1일때, `max_stride(S_1) >= stride(p_1) >= max_pass_value(S_1) - pass_value(p_1) = 0`이고, stride 값은 항상 양수이므로 n=1일때 (1)이 참이다.(4)  
(3)이 참이라 가정했을 때, `max_stride(S_n+1) >= max_pass_value(S_n+1) - pass_value(p_n+1)`임을 보인다. (5)  
stride scheduling의 정의에 따라 `pass_value(p_n+1) = pass_value(p_n) + stride(p_n)`가 성립한다.  
역시 stride scheduling의 정의에 따라 pass value는 항상 증가하므로, `max_pass_value(S_n+1) = max(max_pass_value(S_n), pass_value(p_n) + stride(p_n))` 이 성립한다.
즉, `max_stride(S_n+1) >= max(max_pass_value(S_n), pass_value(p_n) + stride(p_n)) - pass_value(p_n) - stride(p_n)`임을 증명하면 (5)를 참이라 할 수 있다. (6)  

1. `max_pass_value(S_n) >= pass_value(p_n) + stride(p_n)`인 경우
`max_stride(S_n+1) >= max_pass_value(S_n) - pass_value(p_n) - stride(p_n)`
(3)이 성립한다 가정했으므로, `stride(p_n) >= max_pass_value(S_n) - pass_value(p_n)`가 참이다. 이는 `max_pass_value(S_n) - pass_value(p_n) - stride(p_n) <= 0`와 동치이다.  
즉 `max_stride(S_n+1) >= a (a<=0)`이 참이라면 현재 경우에서 (6)이 참이 되는데, stride 값은 항상 양수이므로, 0또는 음수인 a보다 항상 크다. 따라서 현재 경우에서 (6)이 참이다.

2. `max_pass_value(S_n) < pass_value(p_n) + stride(p_n)`인 경우
`max_stride(S_n+1) >= pass_value(p_n) + stride(p_n) - pass_value(p_n) - stride(p_n) = 0`  
stride 값은 항상 양수이므로, 현재 경우에서 (6)이 참이다.

두 경우 모두 (6)이 참이므로, (6)은 참이다.  
즉, n=1일 때 가정 (1)이 참이었고, n일때 (1)이 참이라고 가정했을 때 n+1일때 역시 (1)이 참이라는 것을 위에서 증명했으므로 가정 (1)은 수학적 귀납법에 의해 모든 자연수 n에 대해 성립한다.

---

위 증명을 통해, `C = maximum stride`로 설정하면 `minimum pass value + C >= maximum pass value`를 만족할 만큼 커진다는 것이 증명되었습니다.  
또한 `C`는 너무 크지도 않습니다. 왜냐하면 어차피 `pass value`에 `stride`가 더해지는 것은 `stride scheduling` 과정에서 이뤄지는 자연적인 일이기 때문입니다.

```c
ss->totalusage += usage;
if (ss->minusage > usage)
{
  ss->minusage = usage;
}

int min = ss->pq.size ? pqtop(&ss->pq).key : 0;

struct pqelement element;
element.key   = min + ss->stride[ss->minusage];
element.value = value;
element.usage = usage;
```

위에서 증명한 대로, minimum pass value(`min`)에 maximum stride(`ss->stride[ss->minusage]`)를 더하게 구현했습니다.

### stridetop

일반적인 stride scheduler의 경우, RUNNABLE 한 프로세스를 골라 출력하도록 했습니다.  

```c
if (!ss->pq.size)
{
  return 0;
}

int minidx      = -1;
double minvalue = ss->pq.data[ss->pq.size - 1].key;

for (int i = 0; i < ss->pq.size; ++i)
{
  if (ss->pq.data[i].key <= minvalue &&
      ((struct proc*)ss->pq.data[i].value)->state == RUNNABLE)
  {
    minvalue = ss->pq.data[i].key;
    minidx   = i;
  }
}
if (minidx == -1)
{
  return 0;
}

struct proc* result = ss->pq.data[minidx].value;
ss->pq.data[minidx].key += ss->stride[(int)ss->pq.data[minidx].usage];
pqshiftdown(&ss->pq, minidx);

return result;
```

하지만 scheduler가 master scheduler일 경우, RUNNABLE 한지 여부를 확인할 필요 없이 pass value만 가지고 mlfq scheduler, stride scheduler를 선택하도록 하면 됩니다.  
그래서 `ss`가 `masterscheduler`를 가리키고 있는지 확인해서 두 경우를 나누도록 했습니다.  

```c
if (ss == &masterscheduler)
{
  struct pqelement result = pqtop(&ss->pq);
  result.key += ss->stride[(int)result.usage];
  pqupdatetop(&ss->pq, result);
  return result.value;
}
else
{
  // RUNNABLE한 프로세스만 선택
}
```

## MLFQ/Stride

Milestone1에서 언급했던 아래 규칙에 맞게 구현했습니다.  

1. MLFQ Scheduler는 Main Scheduler에서 100의 Ticket을 갖는다.
2. 만약 Stride Scheduler에 process가 push될 경우, 해당 process가 요구한 Ticket 만큼 MLFQ Schduler의 Ticket을 감소시키고, Stride Scheduler의 Ticket을 증가시킨다.
3. Stride 알고리즘과 유사한 원리로, MLFQ Scheduler와 Stride Scheduler의 CPU 점유율을 Ticket의 개수에 맞게 조정한다
4. MLFQ Scheduler의 Ticket은 20보다 작아질 수 없다

### scheduler

위 규칙에 맞게 scheduler 함수를 수정했습니다.  

```c
strideinit(&masterscheduler, 100);
stridepush(&masterscheduler, (void*)SCHEDMLFQ, 100);

strideinit(&mainstride, 80);
```

MLFQ-Stride 사이의 점유율 조정을 담당하는 `masterscheduler`를 초기화한 이후, 안에 MLFQ Scheduler를 넣고 점유율을 100%로 맞춰줍니다.  
그 다음으로는 stride scheduling을 담당하는 `mainstride`를 초기화합니다.  

```c
for (;;)
{
  sti();
  acquire(&ptable.lock);

  schedidx = (int)stridetop(&masterscheduler);

  // expired가 true일 때 스케줄러에서 p을 받아옴
  if (expired || p->state != RUNNABLE || p->schedule.sched != schedidx)
  {
    // update schedidx
    switch (schedidx)
    {
    case SCHEDMLFQ: // mlfq
      p = mlfqtop();
      if (p)
      {
        break;
      }
    case SCHEDSTRIDE: // stride
      p = stridetop(&mainstride);
      if (p)
      {
        schedidx = 1;
        break;
      }
    default:
      break;
      // no process
    }
  }
```

`masterscheduler`에서 stride scheduling 규칙을 이용해 MLFQ-Stride 중 하나를 선택합니다.  
p가 더는 유효하지 않거나 `masterscheduler`에 의해 선택된 scheduler가 변했다면, 새로운 p를 할당받게 됩니다.  
MLFQ가 선택되었다면 `mlfqtop`을 호출해 MLFQ scheduling으로 p를 결정하고, STRIDE가 선택되었다면 `stridetop`을 호출해 stride scheduling으로 p를 결정합니다.  

```c
  if (p)
  {
    uint start = 0;
    uint end   = 0;

    if (p->state == RUNNABLE)
    {
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      start    = sys_uptime();
      swtch(&(c->scheduler), p->context);
      end = sys_uptime();
      switchkvm();
    }

    switch (p->schedule.sched)
    {
    case SCHEDMLFQ:
      expired = mlfqnext(p, start, end);
      break;

    case SCHEDSTRIDE:
      expired = 1;
      break;

    default:
      panic("scheduler: invalid schedidx");
    }

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;
  }

  release(&ptable.lock);
}
```

`p`가 RUNNABLE 하다면 `p` 프로세스에 자원을 넘겨줍니다.  
그 다음 `p`가 속한 scheduler에 따라 다른 작업을 합니다.  
MLFQ scheduler에 속해 있다면 `mlfqnext`를 호출해 MLFQ scheduling을 진행합니다.  
Stride scheduler는 `stridetop`을 하는 순간 pass value가 변하도록 되어있기 때문에, 바로 `expired`를 1로 만들어줍니다.  

### allocproc

```c
found:
  p->state = EMBRYO;
  p->pid   = nextpid++;

  // mlfq에 p 추가
  // 최대 process 개수 == mlfq level 0의 크기
  // 따라서 mlfqpush가 실패하면 logic error

  if (mlfqpush(p))
  {
    panic("allocproc: mlfqpush failure");
  }

  release(&ptable.lock);
```

프로세스 추가는 `allocproc` 함수에서 진행됩니다.  
그래서 `allocproc` 함수 내에서 `mlfqpush`를 호출해 MLFQ scheduler에 프로세스를 추가하도록 했습니다.  

```c
// Allocate kernel stack.
if ((p->kstack = kalloc()) == 0)
{
  p->state = UNUSED;
  acquire(&ptable.lock);
  schedremoveproc(p);
  release(&ptable.lock);
  return 0;
}
```

kernel stack 할당에 실패해 `p`가 UNUSED로 변하는 경우, `p`를 MLFQ scheduler에서 지워줍니다.

```c
// Copy process state from proc.
if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
{
  kfree(np->kstack);
  np->kstack = 0;
  np->state  = UNUSED;
  acquire(&ptable.lock);
  schedremoveproc(np);
  release(&ptable.lock);
  return -1;
}
```

비슷하게 `copyuvm`에 실패했을 때 역시 `p`를 MLFQ scheduler에서 지워줍니다.  

### schedremoveproc

```c
// int wait(void)
if (p->state == ZOMBIE)
{
  // Found one.
  pid = p->pid;
  kfree(p->kstack);
  p->kstack = 0;
  freevm(p->pgdir);
  p->pid     = 0;
  p->parent  = 0;
  p->name[0] = 0;
  p->killed  = 0;
  p->state   = UNUSED;
  schedremoveproc(p);
  release(&ptable.lock);
  return pid;
}
```

wait 함수에서 ZOMBIE가 된 자식 프로세스를 지워줄 때, `p`를 scheduler에서도 지워줘야 합니다.  
`schedremoveproc`은 `p`를 `p`가 속한 scheduler에서 지워주는 역할을 갖고 있습니다.  
Stride scheduler에서 지울 경우, Master scheduler에서 적절하게 MLFQ-Stride ticket을 바꿔줘야 합니다.  

```c
void
schedremoveproc(struct proc* p)
{
  switch (p->schedule.sched)
  {
  case SCHEDMLFQ:
    mlfqremove(p);
    break;
  case SCHEDSTRIDE: {
    int usage = strideremove(&mainstride, p);
    assert(usage == -1, "usage == -1");

    int mlfqidx      = stridefindindex(&masterscheduler, (void*)SCHEDMLFQ);
    int mlfqusage    = (int)masterscheduler.pq.data[mlfqidx].usage;
    int newmlfqusage = mlfqusage + usage;

    int strideidx      = stridefindindex(&masterscheduler, (void*)SCHEDSTRIDE);
    int strideusage    = (int)masterscheduler.pq.data[strideidx].usage;
    int newstrideusage = strideusage - usage;

    assert(newmlfqusage + newstrideusage != masterscheduler.maxticket ||
               newmlfqusage < 20 || newstrideusage < 0,
           "invalid usage rate");

    if (newstrideusage)
    {
      assert(stridechangeusage(&masterscheduler, strideidx, newstrideusage),
             "strideidx change failure");
      assert(stridechangeusage(&masterscheduler, mlfqidx, newmlfqusage),
             "mlfqidx change failure");
    }
    else
    {
      assert(strideremove(&masterscheduler,
                          masterscheduler.pq.data[strideidx].value) == -1,
             "strideremove stridescheduler failure");
      stridechangeusage(&masterscheduler, 0, 100);
    }

    strideupdateminusage(&masterscheduler);
    break;
  }
  default:
    assert(1, "invalid sched");
  }
}
```

### set_cpu_share

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
STRIDE(10%), cnt : 133239
STRIDE(15%), cnt : 201442
STRIDE(5%), cnt : 68517
STRIDE(5%), cnt : 67360
STRIDE(45%), cnt : 602192
MLfQ(compute), cnt : 53957
MLfQ(compute), cnt : 56426
MLfQ(compute), cnt : 55085
MLfQ(compute), cnt : 56844
MLfQ(compute), cnt : 64833

 --- test_scheduler result --- 
total: 1359895
 0. STRIDE(5%) : 4.9%
 1.   MLFQ(4%) : 4.1%
 2. STRIDE(5%) : 5.0%
 3. STRIDE(10%) : 9.7%
 4.   MLFQ(4%) : 4.0%
 5. STRIDE(15%) : 14.8%
 6.   MLFQ(4%) : 3.9%
 7. STRIDE(45%) : 44.2%
 8.   MLFQ(4%) : 4.1%
 9.   MLFQ(4%) : 4.7%
 MLFQ TOTAL(20%) : 21.1%
[ TEST 80 : 20 test FINISHED ]
// test master end //
```

모두 설정된 비율과 비슷하게 CPU 자원을 할당받고 있음을 확인할 수 있습니다.

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