#ifndef RWLOCK_H
#define RWLOCK_H

#include "spinlock.h"

// Reader-writer lock with writer preference.
struct rwlock {
  struct spinlock lk;  // Protects internal lock state
  int readers;         // Number of active readers
  int writer;          // 1 if a writer holds the lock
  int waiting_writers; // Number of writers waiting
  char *name;          // Lock name for identification
};

void initrwlock(struct rwlock *lock, char *name);
void acquireread(struct rwlock *lock);
void releaseread(struct rwlock *lock);
void acquirewrite(struct rwlock *lock);
void releasewrite(struct rwlock *lock);

#endif // RWLOCK_H