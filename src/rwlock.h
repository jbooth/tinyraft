#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#pragma once

/**
 * Supports readlock, writelock, and wait_write functions.
 * Holders of readlock can wait_write to atomically 
 * release readlock and block until wrunlock is called.
 */
typedef struct traft_rwlock_t {
  pthread_mutex_t m;
  pthread_cond_t  c;
  int             writer; // 1 if writer is waiting for or has acquired lock
  int             num_readers;    // current number of readers
  uint32_t        writeno; // Random number set after each write, used to wait on a change 
} traft_rwlock_t;

int traft_wrlock_init(traft_rwlock_t *rwlock);

int traft_wrlock_destroy(traft_rwlock_t *rwlock);

int traft_rwlock_rdlock(traft_rwlock_t *rwlock);

int traft_rwlock_wrlock(traft_rwlock_t *rwlock);

int traft_rwlock_rdunlock(traft_rwlock_t *rwlock);

/** Releases writelock and notifies all readers blocked on wait_write */
int traft_rwlock_wrunlock(traft_rwlock_t *rwlock);

/**
 * Atomically releases readlock and blocks until wrunlock is called, 
 * reacquiring readlock before returning.  Similar to pthread_cond_wait.
 */
int traft_rwlock_waitwrite(traft_rwlock_t *rwlock);
