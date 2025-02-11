# int pfileread(struct file \*f, char \*addr, int n, int off)

```c
int
pfileread(struct file *f, char *addr, int n, int off)
{
  int r;

  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    ilock(f->ip);
    r = readi(f->ip, addr, off, n);
    iunlock(f->ip);
    return r;
  }
  panic("pfileread");
}
```

원래 fileread코드에서는 readi를 할 때, f->off를 가져가서 read에 성공할 시에 f->off += r을 통해 offset을 증가시켰다. 

하지만 우리 과제에서의 조건은 file offset은 변환하지 않고 지정된 offset에서부터 n값 만큼 읽어오는 동작을 구현하는 것이다.

따라서 원래 있었던 if문을 삭제하고, f->off가 아닌 off를 넘겨주는 형식으로 구현하였다.

offset의 오류 핸들은 readi에서 처리한다.

# int pfilewrite(struct file \*f, char \*addr, int n, int off)

```c
int
pfilewrite(struct file *f, char *addr, int n, int off)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      r = writei(f->ip, addr + i, off, n1);
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short pfilewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("pfilewrite");
}
```

원래 filewrite에서도 r이 0보다 크면 f->off를 r만큼 증가시켜주는 동작을 했다. 그 부분을 뺴고, 지정된 offset을 writei 인자로 넘겨주도록 코드를 변경시켰다.

마찬가지로 큰 offset에 대해서는 writei 내부에서 error handle을 한다.

