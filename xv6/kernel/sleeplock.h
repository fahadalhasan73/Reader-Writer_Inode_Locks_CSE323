// Long-term locks for processes
struct sleeplock {
  uint locked;        // Is the lock held? Indicates whether the sleeping lock is held.
  struct spinlock lk; // spinlock protecting this sleep lock. A spinlock used internally to protect the sleeping lock's state.

  // For debugging:
  char *name; // Name of lock.
  int pid;    // Process holding lock
};
