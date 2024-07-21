#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

int main(int argc, char* argv[])
{
  if(argc < 2)
  {
    fprintf(2, "xargs: missing argument");
    exit(1);
  }
  
  char* cmdargs[MAXARG]; // xargs中传递给每一行运行的程序的参数
  // 将xargs中的指令复制到cmdargs中
  for(int i = 1; i < argc; i++)
  {
    cmdargs[i - 1] = argv[i];
  }
  int argcnt = argc - 1; // 记录参数数量
  char buf[512];
  char ch;
  char* argstart = buf;
  char* argend = buf;
  
  while(read(0, &ch, 1) > 0)
  {
    if(ch == ' ') // 空格标记参数分界，将上一个参数放入数组
    {
      *argend = '\0';
      cmdargs[argcnt++] = argstart;
      argstart = ++argend;
    }
    else if(ch == '\n' || ch == '\0')
    {
      *argend = '\0';
      cmdargs[argcnt++] = argstart;
      cmdargs[argcnt] = 0; // 标记最后一个参数
      
      // 执行程序
      if(fork() == 0)
      {
        exec(argv[1], cmdargs);
      }
      else
      {
        wait(0);
      }
      argstart = argend = buf; // 重置参数
      argcnt = argc - 1;
    }
    else
    {
      *argend = ch;
      argend++;
    }
  }
  exit(0);
}
