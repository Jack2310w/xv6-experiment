#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main()
{
  int p1[2]; // 管道1
  int p2[2]; // 管道2
  pipe(p1);
  pipe(p2);
  int pid; // 用于存储进程id
  
  if(fork() == 0)
  {
    // 子进程的操作
    pid = getpid();
    char buf; // 用于接收管道数据
    // 关闭不需要的fd
    close(p1[1]);
    close(p2[0]);
    
    read(p1[0], &buf, 1); // 读取父进程的数据
    printf("%d: received ping\n", pid);
    write(p2[1], &buf, 1); // 向父进程发送数据
    
    exit(0);
  }
  else
  {
    // 父进程的操作
    pid = getpid();
    char buf = 0; // 要传输的字节
    char rdbuf; // 读取的字节
    // 关闭不需要的fd
    close(p1[0]);
    close(p2[1]);
    
    write(p1[1], &buf, 1); // 向子进程发送数据
    read(p2[0], &rdbuf, 1); // 读取子进程传输的数据
    printf("%d: received pong\n", pid);
    
    exit(0);
  }
}
