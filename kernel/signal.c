
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

uint64
sys_sigalarm(void) {
  int ticks = 0;
  uint64 handler = 0;

  argint(0, &ticks);
  argaddr(1, &handler);

  struct proc* p = myproc();
  if (p == 0) {
    return -1;
  }

  if (ticks == 0 && handler == 0) {
    p->sig_handler_set = 0;
  } else {
    p->sig_interval = ticks;
    p->sig_handler = handler;
    p->sig_accu_ticks = 0;
    p->sig_handler_set = 1;
  }

  return 0;
}

uint64
sys_sigreturn(void) {
  struct proc* p = myproc();

  p->trapframe[0] = p->trapframe[1];
  p->sig_handler_enabled = 1;

  return p->trapframe->a0;
}