#include "types.h"
#include "riscv.h"
#include "rwlock.h"

#include "defs.h"


void
initrwlock(struct rwlock *lock, char *name)
{
  initlock(&lock->lk, "rwlock");

  lock->readers = 0;
  lock->writer = 0;
  lock->waiting_writers = 0;
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

  lock->waiting_writers++;

  while (lock->writer || lock->readers > 0) {
    sleep_prepare(lock);
    release(&lock->lk);
    sleep();
    acquire(&lock->lk);
  }

  lock->waiting_writers--;
  lock->writer = 1;

  release(&lock->lk);
}


void
releasewrite(struct rwlock *lock)
{
  acquire(&lock->lk);

  if (lock->writer == 0)
    panic("releasewrite");

  lock->writer = 0;

  wakeup(lock);

  release(&lock->lk);
}