#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

#define MAX_PATH_LEN 512

void find(char* fileName, char* pathBuf)
{
  int fd = open(pathBuf, O_RDONLY);
  if(fd < 0)
  {
    fprintf(2, "find: cannot open %s\n", pathBuf);
    return;
  }
  
  struct dirent de;
  struct stat st;
  
  if(fstat(fd, &st) < 0)
  {
    fprintf(2, "find: cannot stat %s\n", pathBuf);
    return;
  }
  
  if(st.type != T_DIR)
  {
    fprintf(2, "find: %s is not a directory\n", pathBuf);
    return;
  }
  
  if(strlen(pathBuf) + 1 + DIRSIZ + 1 > MAX_PATH_LEN)
  {
    printf("Path too long\n");
    return;
  }
  
  // 修改基准路径
  char* pathPos = pathBuf + strlen(pathBuf);
  *pathPos = '/';
  pathPos++;
  
  // 遍历文件夹
  while(read(fd, &de, sizeof(de)) == sizeof(de))
  {
    if(de.inum == 0)
      continue;
    
    memmove(pathPos, de.name, DIRSIZ);
    pathPos[DIRSIZ] = 0;
    if(stat(pathBuf, &st) < 0)
    {
      printf("cannot stat %s\n", pathBuf);
      continue;
    }
    
    switch(st.type)
    {
      case T_DEVICE:
      case T_FILE:
        if(strcmp(de.name, fileName) == 0) // 文件：检查是否相等
        {
          printf("%s\n", pathBuf);
        }
        break;
      case T_DIR: // 文件夹：执行递归查找
        if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
          continue;

        find(fileName, pathBuf);
        break;
    }
  }
  // 退出递归时，将当前文件夹移出路径
  char* p = pathBuf + strlen(pathBuf);
  while(p > pathBuf && *p != '/')
  {
    p--;
  }
  *p = 0;
}

int main(int argc, char* argv[])
{
  if(argc < 3) // 缺少参数
  {
    fprintf(2, "find: Missing arguments, usage: find [directory] [filename]\n");
    exit(1);
  }
  
  if(strlen(argv[1]) > MAX_PATH_LEN)
  {
    printf("path too long\n");
    exit(1);
  }
  
  char pathBuf[MAX_PATH_LEN];
  memmove(pathBuf, argv[1], strlen(argv[1]));
  find(argv[2], pathBuf);
  exit(0);
}
