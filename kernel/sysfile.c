//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

uint64 sys_mmap(void)
{
  // 获取用户区参数
  uint64 addr;
  size_t len;
  int prot, flags, fd;
  int offset;
  struct file* f;
  argaddr(0, &addr);
  argaddr(1, &len);  // 此处是为了获取uint64参数
  argint(2, &prot);
  argint(3, &flags);
  argfd(4, &fd, &f);
  argint(5, &offset);
  
  if(fd < 0 || f == 0){
    return (uint64)-1;
  }
  
  struct proc* p = myproc();
  
  if(addr == 0){
    // 找到一个可用的虚拟地址
    for(uint64 va = 0; va < MAXVA; va += PGSIZE){
      if(walk(p->pagetable, va, 0) == 0){
        addr = va;
        break;
      }
    }
  }
  // printf("va = %p\n", addr);
  
  // 找到一个空闲的vma
  for(int i = 0; i < VMASIZE; i++){
    if(!p->vmaarr[i].valid){
      // 申请页面
      struct vma* target = &p->vmaarr[i]; 
      target->pflags = PTE_U;
      if(prot & PROT_READ){
        if(!f->readable){
          return (uint64)-1;
        }
        target->pflags |= PTE_R;
      }
      if(prot & PROT_WRITE){
        if(!f->writable && !(flags & MAP_PRIVATE)){
          return (uint64)-1;
        }
        target->pflags |= PTE_W;
      }
      if(prot & PROT_EXEC){
        target->pflags |= PTE_X;
      }
      
      // 映射页面
      for(uint j = addr; j < addr + len; j += PGSIZE){
        if(mappages(p->pagetable, j, PGSIZE, 0, PTE_U) < 0){
          return (uint64)-1;
        }
      }
      target->addr = addr;
      target->len = len;
      target->f = f;
      target->fflags = flags;
      filedup(f); // 增加f文件的引用数
      target->valid = 1;
      // printf("alloc %p %d\n", addr, len);
      return addr;
    }
  }
  // 没有空闲的vma，分配失败
  return (uint64)-1;
}

/*
void pg_writeback(struct file* f)
{
  if(f->type == FD_PIPE){
      ret = pipewrite(f->pipe, addr, n);
  }
  else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[target->f->major].write(1, addr, n);
  }
  else if(f->type == FD_INODE){
    begin_op();
    ilock(f->ip);
    if ((r = writei(f->ip, 1, addr + i, f->off, PGSIZE)) > 0)
      f->off += r;
      iunlock(f->ip);
      end_op();
    }
    
  }
  else {
    panic("pg_writeback");
  }
}
*/

uint64 munmap(uint64 addr, size_t len)
{
  struct proc* p = myproc();
  // 找到需要unmap的块
  struct vma* target = 0;
  // printf("unmap %p %d\n", addr, len);
  
  for(int i = 0; i < VMASIZE; i++){
    if(p->vmaarr[i].valid && addr >= p->vmaarr[i].addr 
    && addr < p->vmaarr[i].addr + p->vmaarr[i].len){
      target = &p->vmaarr[i];
      break;
    }
  }
  
  if(target == 0){
    return -1; // 没有找到对应页面
  }
  
  pte_t* pte;
  uint64 pa;
  for(uint64 j = addr; j < addr + len; j += PGSIZE){
    // 检查并执行写回操作
    pte = walk(p->pagetable, j, 0);
    if(pte == 0){
      return -1;
    }
    pa = PTE2PA(*pte);
    if(pa){
      if((target->fflags & MAP_SHARED) && (*pte & PTE_D)){
        filewrite(target->f, j, PGSIZE);
      }
      // printf("free %p %p\n", *pte, pa);
      kfree((void*)pa);
    }
  }
  uvmunmap(p->pagetable, addr, len / PGSIZE, 0);
  target->len -= len;
  if(addr == target->addr){
    target->addr = addr + len;
  }
  if(target->len == 0){
    // 释放了整个段
    fileclose(target->f);
    target->valid = 0;
  }

  return 0;
}

uint64 sys_munmap(void)
{
  // 获取参数
  uint64 addr;
  size_t len;
  argaddr(0, &addr);
  argaddr(1, &len);  // 此处是为了获取uint64参数
  return munmap(addr, len);
}

// 处理文件
int handle_pgfread(uint64 va){
  // 查找
  struct proc* p = myproc();
  for(int i = 0; i < VMASIZE; i++){
    if(p->vmaarr[i].valid && va >= p->vmaarr[i].addr 
    && va < p->vmaarr[i].addr + p->vmaarr[i].len){
      // 分配空间
      uint64 mem;
      if((mem = (uint64)kalloc()) == 0){
        // printf("kalloc fail\n");
        return -1;
      }
      memset((char*)mem, 0, PGSIZE);
      // printf("realloc %p %p\n", va, mem);
      uvmunmap(p->pagetable, va, 1, 0);
      
      // 读取文件内容
      ilock(p->vmaarr[i].f->ip);
      int ret = readi(p->vmaarr[i].f->ip, 0, mem, va - p->vmaarr[i].addr, PGSIZE);
      iunlock(p->vmaarr[i].f->ip);
      if(ret < 0){
        kfree((void*)mem);
        return -1;
      }
      // printf("f-pflags: %d\n", p->vmaarr[i].pflags);
      if(mappages(p->pagetable, va, PGSIZE, mem, p->vmaarr[i].pflags) < 0){
        kfree((void*)mem);
        return -1;
      }
      
      return 0;
    }
  }
  return -1;
}

