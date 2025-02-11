# Process

프로세스는 컴퓨터에서 연속적으로 실행되고 있는 컴퓨터 프로그램을 말한다. 

CPU에서 돌아가는 작업(Task)의 **abstraction**이라고 볼 수도 있다.

**프로그램은** 일반적으로 하드 디스크 등에 저장되어 있는 실행코드를 뜻하고, **프로세스는** **프로그램을** 구동하여 메모리에 올라갔을때의 상태를 이야기한다.

# Thread

**스레드는** 어떠한 **프로그램** 내에서 즉, 프로세스 내에서 실행되는 흐름의 단위를 말한다. 

일반적으로 한 프로그램은 **하나의** **스레드**(single execution flow)을 가진다. 때때로, 프로그램 환경에 따라 **둘 이상의 스레드**를 가질 수 있다.

# Differences in context switching

프로세스끼리는 scheduler를 이용한 context switch를 통해 execution flow를 전환한다.

프로세스끼리의 context switch가 일어날 때 PC (program counter), register set 의 교체가 일어난다. 이때 스레드와 구분되는 중요한 특징은 페이지 테이블이 교체가 된다는 것이다. 이는 context switch시에 들어가는 overhead cost를 높힌다. 

x86기준 cr3 register가 가리키고 있었던 page directory base가 바뀌면서 TLB(Translation Lookaside Buffer)를 Flush 해주어야 하고 이는 switch시 새로 바뀐 프로세스 내에서 초반에 TLB miss가 많이 일어난다.

따라서 Disk에서 다시 값을 읽어들이는 데에 시간을 사용해야하기 때문에 성능하락이 일어날 수 있다.

스레드 끼리의 context switch는 프로세스의 context switch보다 overhead가 적게 든다. 

멀티 스레드 환경에서는 한 프로세스 내의 메모리를 공유해 사용할 수 있다. 각 스레드는 프로세스 메모리 구조에서의 Code(text), Data부분과 Heap영역을 공유한다. 각 스레드는 본인만의 Stack영역을 가지고 있다.

따라서 switching시 페이지 테이블을 바꿔줄 필요가 없고 이는 같은 조건일 때 스레드의 context switch가 cost가 적다고 이야기할 수 있다.

![운영체제] 프로세스 메모리 구조](https://media.vlpt.us/images/cchloe2311/post/9a74f36f-fc70-4292-8302-9884f9826987/image.png)

# POSIX thread

## int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void \*(\*start_routine)(void *),void *arg)

### pthread_t *thread

호출이 성공하면 pthread_create는 위 주소가 가리키는 메모리 장소에 새로 생성된 스레드의 스레드 ID를 저장한다

### const pthread_attr_t *attr

attr 인수는 스레드의 여러 특성을 설정하는 데 쓰인다. 이 인수에 NULL을 지정하면 스레드가 기본 특성들로 생성된다.

### void *(*start_routine)(void *), void *arg

 새로 생성된 스레드는 start routine인수로 지정된 함수의 주소에서 실행을 시작한다. 그 함수는 인수를 받는데, arg인수에 지정된 포인터가 그 인수로 전달된다. start routine 함수에 여러 개의 정보를 전달하고 싶다면 그 정보들을 구조체에 저장해 두고 그 구조체의 주소를 arg에 지정하면 된다.

## int pthread_join(pthread_t thread, void **ret_val)

### pthread_t thread

호출 스레드는 thread 인수로 지정된 스레드가 pthread_exit을 호출하거나, start routine에서 리턴되거나, 취소될 때까지 차단된다. thread 인수가 start routine에서 정상적으로 리턴되었다면

### void **ret_val

ret_val 포인터를 활용해 리턴 값을 받아올 수 있다.

## void pthread_exit(void *ret_val)

### void *ret_val

현재 진행중인 스레드를 종료시킨다. ret val은 pthread join에서 받아 쓸 수 있다.

# Design Basic LWP Operations for xv6

## Variables

```c
// proc.h
struct proc {
    // ...
    struct threadNODE *head
    int tid;
    struct proc *master;
    void *ret_val;
    
}
```

```c
// types.h
// ...
typedef int thread_t;
```

### proc

- int tid

스레드 ID를 뜻한다. 0일 경우 master thread이다.

- struct proc *master

마스터 스레드를 가리키는 포인터다.

- void *ret_val

thread_exit(void * ret_val) 을 할 때, 인자로 받은 ret_val을 스레드의 ret_val 변수에 저장할 수 있게 한다. 이 변수 ret_val은 후에 thread_join에서 사용된다.

## Newly added Functions

### int thread_create(thread_t * thread, void * (*start_routine)(void *), void *arg)

thread_t 포인터 변수에 현재 tid 값을 할당한다.

start_routine 함수 포인터를 thread_create안 tf 빌드시 eip에 할당해준다.

arg 포인터는 exec에서 보았던 것 처럼 sp 값을 내려주고 arg를 스택에 푸쉬해준뒤에 fake return PC를 넣어주는 형식으로 활용한다.

함수 성공시 thread_create는 0을 리턴한다. 에러가 날 시 -1을 리턴한다.

### void thread_exit(void *retval)

인자로 넘어온 retval을 현재 스레드의 ret_val 변수에 넣어준다. 그리고 원래 exit에서 핸들되는 것 처럼 현재 스레드의 state를 ZOMBIE상태로 바꿔준다.

### int thread_join(thread_t thread, void **retval)

threadID로 입력된 tid를 활용해 ptable을 순회하며 찾는다. 찾은 thread의 state가 ZOMBIE인지 확인한다. 

이후 인자로 들어온 void **retval에 thread가 가지고 있던 ret_val변수를 이용하여 리턴 값을 저장해준다.

그 스레드에 할당된 메모리를 free해주고, 나머지 변수를 0으로 바꿔준 뒤, 상태를 UNUSED로 바꿔준다.

이는 wait syscall의 동작을 모방한 것이다.

## Original Functions

### Exit

thread 내에서 exit을 부르면 struct proc* master을 활용하여 마스터 스레드에 접근한다. 이후 ptable을 순회하며 해당하는 threads를 clean up 한다.

### Fork

thread 내에서 fork을 부르면 호출한 스레드는 마스터 thread가 되고 그 스레드가 싱글 스레드로 있는 하나의 프로세스가 새롭게 생성된다. 

### Exec

exec이 불려지면 exec을 호출한 스레드가 마스터 thread가 되고 본인을 제외한 다른 threads를 clean up한다. 그리고 exec 과정을 다시 진행한다.

### Sbrk

When multiple LWPs simultaneously call the sbrk system call to extend the memory area, memory areas must not be allocated to overlap with each other, nor should they be allocated a space of a different size from the requested size. The expanded memory area must be shared among LWPs.

추후 구현시 추가로 서술하겠음

### Kill

결국 proc을 사용하기 때문에 p->killed = 1하는 방식으로 thread kill을 수행한다. 이는 나중에 trap에서 관리된다.

### Pipe

All LWPs must share a pipe, and reading or writing data should be synchronized and not be duplicated.

추후 구현시 추가로 서술하겠음

### Sleep

이도 결국 같은 proc을 사용하기 때문에 같은 sleep을 사용하면 된다. 다른 함수들 내에서 wakeup의 시점만 잘 결정해주면 된다.

### Interaction with the scheduler

 LWP master thread의 MLFQ, STRIDE 정보를 이용하거나, LWP 자신에게 할당된 MLFQ, STRIDE 정보를 이용하여 자기가 있는 Queue나, STRIDE ticket, time allotment, time quantum 등에 access한다. 그렇게 찾은 정보를 이용하여 틱이 증가할 때 마다 trap에서 master thread가 있는 queue에 주어진 time allotment를 다 썼는지 확인하는 식으로 동작한다.





