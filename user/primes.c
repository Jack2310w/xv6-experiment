#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PRIME_MAX_INPUT 35

// 用于创建右邻居
[[noreturn]]
void createNeighbor(int curNum, int parentRead, int parentPrime)
{
  int p[2]; // 管道
  pipe(p);
  if(fork() == 0) // 子进程
  {
    // 关闭不需要的管道
    close(p[1]);
    // 初始化：从父进程中读取第一个数并输出prime
    int pnum;
    read(p[0], &pnum, 4);
    printf("prime %d\n", pnum);
    // 持续读取管道中的数
    int n;
    while(read(p[0], &n, 4) > 0)
    {
      if(n % pnum != 0)
      {
        createNeighbor(n, p[0], pnum); // 此时子进程没有右邻居，需要创建
      }
    }
    // 只有最后一个进程会执行到此处
    exit(0);
  }
  else // 父进程
  {
    // 关闭不需要的管道
    close(p[0]);
    // 向子进程输出第一个数
    write(p[1], &curNum, 4);
    // 从父进程中读取数并判断质数输入到子进程中
    int n;
    while(read(parentRead, &n, 4) > 0)
    {
      if(n % parentPrime != 0)
      {
        write(p[1], &n, 4);
      }
    }
    close(p[1]);
    // 等待子进程结束后退出
    wait(0);
    exit(0);
  }
}

int main()
{
  int p[2]; // 第一个管道
  pipe(p);
  if(fork() == 0) // 处理prime=2的子进程
  {
    // 关闭不需要的管道
    close(p[1]);
    // 初始化：从父进程中读取第一个数并输出prime
    int pnum;
    read(p[0], &pnum, 4);
    printf("prime %d\n", pnum);
    // 持续读取管道中的数
    int n;
    while(read(p[0], &n, 4) > 0)
    {
      if(n % pnum != 0)
      {
        createNeighbor(n, p[0], pnum); // 此时子进程没有右邻居，需要创建
      }
    }
  }
  else
  {
    // 关闭不需要的管道
    close(p[0]);
    // 向prime=2的子进程中输入所有数
    for(int n = 2; n <= PRIME_MAX_INPUT; n++)
    {
      write(p[1], &n, 4);
      // printf("parent write %d\n", n);
    }
    // 等待子进程结束
    close(p[1]);
    wait(0);
    exit(0);
  }
}
