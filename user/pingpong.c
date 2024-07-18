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
    int fdread = dup(p1[1]); // 读取父进程传输数据的fd
    int fdwrite = dup(p2[0]); // 向父进程传输数据的fd
    char buf; // 用于接收管道数据
    // 关闭不需要的fd
    close(p1[0]);
    close(p1[1]);
    close(p2[0]);
    close(p2[1]);
    
    read(fdread, &buf, 1); // 读取父进程的数据
    printf("%d: received ping\n", pid);
    write(fdwrite, &buf, 1); // 向父进程发送数据
    
    exit(0);
  }
  else
  {
    // 父进程的操作
    pid = getpid();
    int fdwrite = dup(p1[0]); // 向子进程传输数据的fd
    int fdread = dup(p2[1]); // 读取子进程传输数据的fd
    char buf = 0; // 要传输的字节
    char rdbuf; // 读取的字节
    // 关闭不需要的fd
    close(p1[0]);
    close(p1[1]);
    close(p2[0]);
    close(p2[1]);
    
    write(fdwrite, &buf, 1); // 向子进程发送数据
    wait(0);
    read(fdread, &rdbuf, 1); // 读取子进程传输的数据
    printf("%d: received pong\n", pid);
    
    exit(0);
  }
}


