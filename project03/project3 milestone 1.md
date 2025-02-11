# transaction

transaction은 ACID property를 따라야 한다. ACID란 차례로

- [**원자성**(Atomicity)](https://ko.wikipedia.org/wiki/원자성_(데이터베이스_시스템))은 트랜잭션과 관련된 작업들이 부분적으로 실행되다가 중단되지 않는 것을 보장하는 능력이다. 예를 들어, 자금 이체는 성공할 수도 실패할 수도 있지만 보내는 쪽에서 돈을 빼 오는 작업만 성공하고 받는 쪽에 돈을 넣는 작업을 실패해서는 안된다. 원자성은 이와 같이 중간 단계까지 실행되고 실패하는 일이 없도록 하는 것이다.
- **일관성**(Consistency)은 트랜잭션이 실행을 성공적으로 완료하면 언제나 일관성 있는 데이터베이스 상태로 유지하는 것을 의미한다. 무결성 제약이 모든 계좌는 잔고가 있어야 한다면 이를 위반하는 트랜잭션은 중단된다.

- 독립성(Isolation)은 트랜잭션을 수행 시 다른 트랜잭션의 연산 작업이 끼어들지 못하도록 보장하는 것을 의미한다. 이것은 트랜잭션 밖에 있는 어떤 연산도 중간 단계의 데이터를 볼 수 없음을 의미한다. 은행 관리자는 이체 작업을 하는 도중에 쿼리를 실행하더라도 특정 계좌간 이체하는 양 쪽을 볼 수 없다. 공식적으로 고립성은 트랜잭션 실행내역은 연속적이어야 함을 의미한다. 성능관련 이유로 인해 이 특성은 가장 유연성 있는 제약 조건이다. 자세한 내용은 관련 문서를 참조해야 한다.

- **지속성**(Durability)은 성공적으로 수행된 트랜잭션은 영원히 반영되어야 함을 의미한다. 시스템 문제, DB 일관성 체크 등을 하더라도 유지되어야 함을 의미한다. 전형적으로 모든 트랜잭션은 로그로 남고 시스템 장애 발생 전 상태로 되돌릴 수 있다. 트랜잭션은 로그에 모든 것이 저장된 후에만 commit 상태로 간주될 수 있다.

  -출처: 위키피디아 (https://ko.wikipedia.org/wiki/ACID)

이다.

우리는 logging을 할때 begin_op와 end_op로 transaction을 명시해 줄 것이고, log는 transaction의 property를 만족시켜야 한다.

# void end_op(void)

```c
void
end_op(void)
{
  int do_commit = 0;
  int do_log = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){
    log.committing = 1;
    if (log.lh.n + 1 * MAXOPBLOCKS > LOGSIZE)
      do_commit = 1;
    else
      do_log = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    n = 0;
    wakeup(&log);
    release(&log.lock);
  }
  if(do_log) {
    // 파이프 예외처리
    if (log.lh.n > n) {
      append_log();
      append_head();
      n = log.lh.n;
    }
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}
```

위는 end_op함수의 코드다. 원래의 end_op함수는 한 트랜잭션이 끝날 때마다 outstanding값이 0이면, 즉 다른 동시에 돌아가는 transaction이 없을 경우 disk에 do_commit을 통해 write한다. 이는, log영역이 꽉 차지 않았을 때에도 무조건 disk에 접근해야 하기 때문에 성능 저하가 생긴다.

새로 구현한 end_op함수에서는 if (log.lh.n  + 1 * MAXOPBLOCKS > LOGSIZE) 조건문을 통해 현재 outstanding값이 0일때, 로그 영역에 새로운 transaction이 들어오기에 space가 부족하다면 do_commit을 1로 만들어서 commit을 하고, 새로운 transaction을 쓰기에 충분한 space가 있다면, append_log, append_write를 불러 그저 현존하는 log영역에 덧붙히는 작업만 수행한다. 

이를 통해서 로그 영역이 꽉 차지 않았는데도 outstanding값이 0이면 disk에 파일을 쓰는 행위를 방지할 수 있다. 또한 혹시 모를 오동작으로 인해 OS가 종료된다고 하여도 이미 처리된 transaction은 log영역에 기록되어있기 때문에 restore할 수 있다.

이를 통하여 transaction이 끝났을 때 ACID의 D(Durability property)가 만족됨을 알 수 있다.

commit함수에서 log.lh.n이 0보다 클 경우에만 commit이 수행되도록 예외처리가 되어있는데, 이는 begin_op와 end_op가 불리고 그 사이에 아무것도 안 쓰여졌을 때 commit을 할 필요가 없기 때문에 예외처리를 한 것이다.

마찬가지로 우리도 begin_op를 부르고 바로 end_op를 부르는 등의 log영역에 아무것도 쓰지 않는 경우의 예외처리를 해주기 위해 이전의 log.lh.n값을 전역변수 n에 저장하여 변화값이 없을 경우에는 append_log와 append_head함수를 부르지 않는다.

# void append_log(void)

```c
void
append_log(void)
{
  int tail;
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);

  for (tail = hb->n; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
  brelse(buf);
}

```

```c
static void
write_log(void)
{
  int tail;

  // cprintf("loglhn %d\n", log.lh.n);
  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}
```

기존의 write_log의 경우엔, tail을 0부터 log.lh.n까지 순회하면서 log블록을 복사한다. 우리는 transaction을 할때마다, log를 복사할 것인데, write_log를 그대로 사용하면 이미 복사된 앞 부분을 중복해서 복사할 것이다. 이는 추가적인 성능 하락을 일으킨다.

append_log의 경우엔 로그 헤드 블럭을 읽어와서 hb->n값을 읽어와 hb-> n값 에서부터 복사를 시작함을 알 수 있다. 이는 새로운 transaction이 시작하는 곳 에서부터만 복사를 하기 위함이다.

# void append_head(void)

```c
void
append_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  for (i = hb->n; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  hb->n = log.lh.n;
  bwrite(buf);
  brelse(buf);
}

```

```c
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}
```

append_head의 경우에는 현재 hb->n번째 블록부터 log.lh.n까지 block을 복사시킨 후 hb->n = log.lh.n을 불러주어 head를 덮어 씌운다.

기존의 write head의 경우에도 0부터 log.lh.n값까지 무조건 덮어씌우기 때문에 여러 transaction이 log에 남아있다면 불필요한 복사가 일어나 성능 하락이 존재할 수 있다.

# void sync()

```c
void sync()
{
  acquire(&log.lock);
  if (log.committing) {
    sleep(&log, &log.lock);
    release(&log.lock);
    return;
  }

  while (log.outstanding > 0)
  {
    sleep(&log, &log.lock);
  }
  

  log.committing = 1;
  release(&log.lock);

  commit();
  acquire(&log.lock);
  log.committing = 0;
  wakeup(&log);
  release(&log.lock);
}
```

user는 sync를 부르며, 내가 지금까지 작업한 결과가 disk에 쓰여지기를 기대한다. 

sync가 불렸을 때, 이미 다른 transaction이 commit을 하고 있다면 outstanding값이 0이라는 얘기고 user가 sync를 부른 시점이 end_op를 마친 후라는 이야기므로 user가 적어둔 변경사항은 log영역에 남아있다고 생각할 수 있다. 따라서 다른 transaction에서 commit을 한다면 user가 기대하는 동작을 수행한다고 볼 수 있다. 

유저는 sync가 끝난 "시점"에 본인의 작업이 끝났다고 인지할 것이므로, 바로 return 하지 않고, sleep을 통하여 commit이 끝날 때 까지 기다렸다가 return하는 것을 볼 수 있다.

만약 log.commiting이 0이라면 아직 log영역에 변경사항들이 남아있다는 얘기이므로 sync에서 commit을 해주어야 한다. 이때 log.outstanding값이 0보다 크면 지금 log영역을 사용하는 다른 transaction이 있다는 의미이므로 sleep한다. 이후 outstanding값이 0이되면 log영역을 참조하는 transaction이 없다는 의미이므로 commit 작업에 들어간다.

# int get_log_num(void)

```c
int get_log_num(void)
{
  return log.lh.n;
}
```

간단하게 log.lh.n을 리턴하는 것으로 구현하였다.

# other log syscalls

다른 log syscall들 은 그대로 두었다.

과제에서 Exceed log space에 대한 처리를 해주어야 한다고 했는데 transaction이 더이상 들어갈 수 없을 때 end_op에서 commit을 해주도록 처리하였고, 기존의 log_write를 그대로 사용하기 때문에, transaction 자체가 너무 클 경우

```c
if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
```

기존의 코드에 의해 error가 handling된다.