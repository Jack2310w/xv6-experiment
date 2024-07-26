#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  // 获取用户区参数
  uint64 base, mask;
  int len;
  argaddr(0, &base);
  argint(1, &len);
  argaddr(2, &mask);
  
  struct proc* p = myproc();
  const int maxlen = 800;
  char bitmask[100] = {0};
  
  if(len > maxlen){
    return -1; // 超出最大长度
  }

  for(uint64 i = 0; i < len; i++){
    pte_t* pte = walk(p->pagetable, base + i * PGSIZE, 0); // 根据虚拟地址找到页表项
    if(pte == 0){
      return -1;
    }
    uint64 access = (*pte & PTE_A);
    if(access){
      bitmask[i / 8] |= 1 << (i % 8);
    }
    *pte = *pte & ~PTE_A;
  }
  
  if(copyout(p->pagetable, mask, bitmask, (len + 7) / 8) < 0){
    return -1;
  }
  
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
