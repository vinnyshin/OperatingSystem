# file.h

```c
// struct inode

...
  // NDIRECT + Single indirect + Doubly indirect + Triple indirect
  uint addrs[NDIRECT + 1 + 1 + 1];
...
```

# fs.h

```c
// struct dinode

...
  // NDIRECT + Single indirect + Doubly indirect + Triple indirect
  uint addrs[NDIRECT + 1 + 1 + 1];   // Data block addresses
...
```

```c
#define NDIRECT 10
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDOUBLYINDIRECT (NINDIRECT * NINDIRECT)
#define NTRIPLEINDIRECT (NINDIRECT * NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDOUBLYINDIRECT + NTRIPLEINDIRECT)
```

# #define FSSIZE

```c
#define FSSIZE       2500000  // size of file system in blocks
```

우리가 구현한 file system에서 파일의 최대크기는 MAXFILE의 NDIRECT + NINDIRECT + NDOUBLYINDIRECT + NTRIBLEINDIRECT 이다.

이는 10 + 128 + 128 \* 128 + 128 \* 128 \* 128 =  2113674 이다.

file system은 당연히 MAXFILE의 크기보다 커야할 것이라 판단하여, block의 개수를 2500000개로 넉넉히 잡아주었다.

# static uint bmap(struct inode *ip, uint bn)

```c
// bmap
...
    
  bn -= NINDIRECT;

  if(bn < NDOUBLYINDIRECT) {
    if((addr = ip->addrs[NDIRECT + 1]) == 0)
      ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn / NINDIRECT]) == 0){
      a[bn / NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn % NINDIRECT]) == 0){
      a[bn % NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    return addr;
  }

...
```

이 부분은 block number가 NINDIRECT개수 이상일 때 doubly indirect를 사용하는 코드이다. bn에서 NINDIRECT를 빼주면, 해당하는 doubly indirect 영역에서의 index로 변환하기 편하다.

처음엔 NDIRECT + 1이 할당되었는지 확인한다. 할당되지 않았다면 addrs[NDIRECT + 1]에 새로 블록을 할당한다.

이후 addr[NDIRECT + 1] 번째의 주소를 bread를 통해 불러온다.

bn을 NINDIRECT로 나누게 되면 doubly indirect에서 첫번째 level에서의 index가 나온다. 그 인덱스가 비어있다면 새로 할당을 해준다.

이후 bread로 그 인덱스를 불러오고 그 인덱스 내에서 bn % NINDIRECT를 통해 두번째 level index를 불러온다. 해당하는 두번째 index 블록이 할당되어있지 않다면 할당하고, 그 블록의 addr를 리턴한다.

```c
// bmap
...
    
    bn -= NDOUBLYINDIRECT;

  if(bn < NTRIPLEINDIRECT) {
    if((addr = ip->addrs[NDIRECT + 1 + 1]) == 0)
      ip->addrs[NDIRECT + 1 + 1] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn / NDOUBLYINDIRECT]) == 0){
      a[bn / NDOUBLYINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[(bn % NDOUBLYINDIRECT) / NINDIRECT]) == 0){
      a[(bn % NDOUBLYINDIRECT) / NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn % NINDIRECT]) == 0){
      a[bn % NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

...
// bmap fin.
```

TRIPLEINDIRECT도 doubly indirect를 사용할 때와 비슷하게 index를 찾아서 할당해주고 그 addr를 리턴한다.

# static void itrunc(struct inode *ip)

```c
// itrunc
...
    
  if(ip->addrs[NDIRECT + 1]){
    bp1 = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a1 = (uint*)bp1->data;
    for(i = 0; i < NINDIRECT; i++){
      if(a1[i]) {
        bp2 = bread(ip->dev, a1[i]);
        a2 = (uint*)bp2->data; 
        for (j = 0; j < NINDIRECT; j++){
          if(a2[j])
            bfree(ip->dev, a2[j]);
        }
        brelse(bp2);
        bfree(ip->dev, a1[i]);
        a1[i] = 0;
      }
    }
    brelse(bp1);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }

...
```

addr[NDIRECT + 1]이 할당되었다면, 즉 doubly indirect가 지금 사용되고 있다면, level 1, level 2를 순회하며 전부 free 시킨다.

```c
...
    
  if(ip->addrs[NDIRECT + 1 + 1]){
    bp1 = bread(ip->dev, ip->addrs[NDIRECT + 1 + 1]);
    a1 = (uint*)bp1->data;
    for(i = 0; i < NINDIRECT; i++){
      if(a1[i]) {
        bp2 = bread(ip->dev, a1[i]);
        a2 = (uint*)bp2->data;
        for(j = 0; j < NINDIRECT; j++){
          if(a2[j]){
            bp3 = bread(ip->dev, a2[j]);
            a3 = (uint*)bp3->data;
            for (k = 0; k < NINDIRECT; k++)
            {
              if(a3[k])
                bfree(ip->dev, a3[k]);
            }
            brelse(bp3);
            bfree(ip->dev, a2[j]);
            a2[j] = 0;
          }
        }
        brelse(bp2);
        bfree(ip->dev, a1[i]);
        a1[i] = 0;
      }
    }
    brelse(bp1);
    bfree(ip->dev, ip->addrs[NDIRECT + 1 + 1]);
    ip->addrs[NDIRECT + 1 + 1] = 0;
  }

...
// itrunc fin
```

마찬가지로 level 1, level 2, level 3를 순회하며 모두 free해주는 동작이다.