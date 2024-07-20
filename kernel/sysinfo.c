#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64 sys_sysinfo(void)
{
    uint64 info; // user pointer to struct sysinfo
    argaddr(0, &info);

    struct sysinfo sysinfo;
    sysinfo.nproc = calculate_used_proc();
    sysinfo.freemem = calculate_free_mem();

    struct proc *p = myproc();
    if (copyout(p->pagetable, info, (char *)&sysinfo, sizeof(struct sysinfo)) < 0)
    {
        return -1;
    }
    return 0;
}