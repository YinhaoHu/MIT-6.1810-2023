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

uint64
sys_mmap(void) {
  // Assume addr is 0 in lab.
  void* addr;
  // Number of bytes to map, might not be the same len as file's.
  size_t len;
  // prot: PROT_READ or/and PROT_WRITE.
  // flags: MAP_SHARED or MAP_PRIVATE. Write modifications to file or not.
  // fd: open file to map.
  int prot, flags, fd;
  // offset: assume it 0 in lab.
  off_t offset;
  struct file* file;

  argaddr(0, (uint64*)&addr);
  argaddr(1, &len);
  argint(2, &prot);
  argint(3, &flags);
  if (argfd(4, &fd, &file) < 0) {
    printf("[ERROR] sysfile.c/sys_mmap: argfd\n");
    return -1;
  };
  argaddr(5, (unsigned long*)&offset);

  // No rw mapping to a read-only file.
  if ((file->writable == 0) && (prot & PROT_WRITE) && (flags & MAP_SHARED)) {
    return -1;
  }

  struct proc* p = myproc();
  struct vma* vmap;
  int i;

  // find vma
  for (i = 0; i < NOVMA; ++i) {
    if (p->vmas[i].file == 0 && p->vmas[i].addr == 0) {
      vmap = &p->vmas[i];
      break;
    }
  }
  if (i == NOVMA) {
    printf("[ERROR] sysfile.c/sys_mmap: i==NOVMA\n");
    return -1;
  }

  filedup(file);
  // set vma
  vmap->file = file;
  vmap->len = len;
  vmap->flags = flags;
  vmap->addr = p->sz;

  // allocate a virtual address
  size_t aligned_len = PGROUNDUP(len);
  p->sz += aligned_len;

  // do not make the pte valid, make it valid in user trap and alloc mem
  int pte_flags = 0;
  if (prot & PROT_READ) {
    pte_flags |= PTE_R;
  }
  if (prot & PROT_WRITE) {
    pte_flags |= PTE_W;
  }

  uint64 va = vmap->addr;
  if (mappages(p->pagetable, va, aligned_len, 0, pte_flags) < 0) {
    fileclose(vmap->file);
    printf("[ERROR] sysfile.c/sys_mmap: mappages\n");
    return -1;
  };
#ifdef ALLOW_DEBUG
  printf(
      "[DEBUG] sysfile.c/sys_mmap: pid=%d, OK[va_start=%p, va_end=%p].\n", p->pid, vmap->addr, vmap->addr + vmap->len);
#endif
  return vmap->addr;
}

int
check_all_unmapped(struct vma* vmap, pagetable_t pagetable) {
  uint64 va_end = vmap->addr + vmap->len;
  int all_freed = 1;
  pte_t* pte;

  for (uint64 va = vmap->addr; va < va_end; va += PGSIZE) {
    pte = walk(pagetable, va, 0);
    if (*pte) {
      all_freed = 0;
      break;
    }
  }
  return all_freed;
}

uint64
sys_munmap_internal(uint64 addr, size_t size) {
#ifdef ALLOW_DEBUG
  printf("[TRACE] sysfile.c/sys_munmap_internal: addr=%p, size=%p\n", addr, size);
#endif
  struct proc* p = myproc();

  // Find the vmap.
  struct vma* vmap = 0;
  for (int i = 0; i < NOVMA; ++i) {
    vmap = &p->vmas[i];

    if ((vmap->addr <= addr) && ((vmap->addr + vmap->len) >= (addr + size))) {
      if (vmap->file == 0) {
        return 0;
      }
      break;
    }
    vmap = 0;
  }
  if (vmap == 0) {
    printf("[ERROR] sysfile.c/sys_munmap: unexpected addr(%p) and size(%p).\n", addr, size);
    return -1;
  }

  if (vmap->flags & MAP_SHARED) {
    begin_op();
    ilock(vmap->file->ip);

    for (uint64 va = addr; va < addr + size; va += PGSIZE) {
      pte_t* pte = walk(p->pagetable, va, 0);
      if (*pte == 0) {
        continue;
      }
      int res = writei(vmap->file->ip, 1, va, va - vmap->addr, PGSIZE);
      if (res < 0) {
        printf("[ERROR] sysfile.c/sys_munmap: writei for addr=%p, size=%p,res = %d, file=%p\n", addr, size, res,
            vmap->file);
        return -1;
      }
    }
    iunlock(vmap->file->ip);
    end_op();
  }

  int all_unmapped = check_all_unmapped(vmap, p->pagetable);

  if (all_unmapped) {
    return 0;
  }

  uint64 va = addr;
  uint64 va_end = va + size;
  for (; va < va_end; va += PGSIZE) {
    pte_t* pte = walk(p->pagetable, va, 0);
    if (*pte) {
      uvmunmap(p->pagetable, va, 1, 1);
    }
  }

  // Decr the refcnt if removes all.
  if (!all_unmapped) {
    all_unmapped = check_all_unmapped(vmap, p->pagetable);
    if (all_unmapped) {
      fileclose(vmap->file);
      vmap->file = 0;
    }
  }

  return 0;
}

uint64
sys_munmap(void) {
  uint64 addr;
  size_t size;

  argaddr(0, &addr);
  argaddr(1, &size);

  return sys_munmap_internal(addr, size);
}
