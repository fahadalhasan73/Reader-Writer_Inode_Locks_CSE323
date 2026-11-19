#include "types.h"
#include "riscv.h"
#include "rwlock.h"

#include "defs.h"
#include "param.h"
#include "proc.h"


void
initrwlock(struct rwlock *lock, char *name)
{
  initlock(&lock->lk, "rwlock");

  lock->readers = 0;
  lock->writer = 0;
  lock->writer_pid = 0;
  lock->waiting_writers = 0;
  lock->next_ticket = 0;
  lock->serving_ticket = 0;
  lock->name = name;
}

void
acquireread(struct rwlock *lock)
{
  acquire(&lock->lk);

  while (lock->writer || lock->waiting_writers > 0) {
    sleep_prepare(lock);
    release(&lock->lk);
    sleep();
    acquire(&lock->lk);
  }

  lock->readers++;

  release(&lock->lk);
}

void
releaseread(struct rwlock *lock)
{
  acquire(&lock->lk);

  if (lock->readers < 1)
    panic("releaseread");

  lock->readers--;

  if (lock->readers == 0)
    wakeup(lock);

  release(&lock->lk);
}


void
acquirewrite(struct rwlock *lock)
{
  acquire(&lock->lk);

  // Assign a unique ticket to this writer.
  uint my_ticket = lock->next_ticket++;
  lock->waiting_writers++;

  // Wait until it is our turn and the lock is free.
  while (lock->writer ||
         lock->readers > 0 ||
         my_ticket != lock->serving_ticket) {
    sleep_prepare(lock);
    release(&lock->lk);
    sleep();
    acquire(&lock->lk);
  }

  lock->waiting_writers--;
  lock->writer = 1;
  lock->writer_pid = myproc()->pid;

  release(&lock->lk);
}


void
releasewrite(struct rwlock *lock)
{
  acquire(&lock->lk);

  // Ensure the current process owns the writer lock.
  if (lock->writer == 0 ||
      lock->writer_pid != myproc()->pid)
    panic("releasewrite");

  // Release exclusive ownership.
  lock->writer = 0;
  lock->writer_pid = 0;

  // Allow the next writer in FIFO order to proceed.
  lock->serving_ticket++;

  // Wake waiting readers and writers.
  wakeup(lock);

  release(&lock->lk);
}

// Check whether the current process owns the writer lock.
int
holdingwrite(struct rwlock *lock)
{
  int r;

  acquire(&lock->lk);

  r = lock->writer &&
      (lock->writer_pid == myproc()->pid);

  release(&lock->lk);

  return r;
}