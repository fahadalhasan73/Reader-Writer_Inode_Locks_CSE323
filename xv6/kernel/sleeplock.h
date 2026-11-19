#ifndef SLEEPLOCK_H
#define SLEEPLOCK_H

// Long-term locks for processes.
struct sleeplock {
  uint locked;
  struct spinlock lk;
  char *name;
  int pid;
};

#endif // SLEEPLOCK_H