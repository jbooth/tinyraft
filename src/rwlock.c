#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include "rwlock.h"

int traft_rwlock_init(traft_rwlock_t *rwlock) {
  rwlock->writer = 0;
  rwlock->num_readers = 0;
  rwlock->num_readers = 0;
  int err = pthread_mutex_init(&rwlock->m, NULL);
  if (err != 0) { return err; }
  return pthread_cond_init(&rwlock->c, NULL);
}

int traft_rwlock_destroy(traft_rwlock_t *rwlock) {
  int err = pthread_mutex_destroy(&rwlock->m);
  if (err != 0) { return err; }
  return pthread_cond_destroy(&rwlock->c);
}
// Acquire readlock
int traft_rwlock_rdlock(traft_rwlock_t *rwlock) {
  int err = pthread_mutex_lock(&rwlock->m);
  if (err != 0) { return err; }
  // Wait until no writer
  while (rwlock->writer) {
    err = pthread_cond_wait(&rwlock->c, &rwlock->m);
    if (err != 0) { return err; }
  }
  // Acquire readlock
  rwlock->num_readers++;
  return pthread_mutex_unlock(&rwlock->m);
}

// Acquire writelock
int traft_rwlock_wrlock(traft_rwlock_t *rwlock) {
  int err = pthread_mutex_lock(&rwlock->m);
  // Ensure not held
  while(rwlock->writer) {
    err = pthread_cond_wait(&rwlock->c, &rwlock->m);
    if (err != 0) { return err; }
  }
  // Lock and wait for readers to unlock
  rwlock->writer = 1;
  while(rwlock->num_readers) {
    err = pthread_cond_wait(&rwlock->c, &rwlock->m);
    if (err != 0) { return err; }
  }
  return 0;
}

// Release readlock
int traft_rwlock_rdunlock(traft_rwlock_t *rwlock) {
  int err = pthread_mutex_lock(&rwlock->m);
  if (err != 0) { return err; }
  // Decrement readers, wake up all
  rwlock->num_readers--;
  pthread_cond_broadcast(&rwlock->c);
  return pthread_mutex_unlock(&rwlock->m);
}

// Releases writelock and notifies any readers blocked on traft_rwlock_waitwrite
int traft_rwlock_wrunlock(traft_rwlock_t *rwlock) {
  int err = pthread_mutex_lock(&rwlock->m);
  if (err != 0) { return err; }
  // Mark no writer and state changed, wake up all
  rwlock->writer = 0;
  // avoid UB overflow, it just has to change
  if (rwlock->writeno == UINT32_MAX) { rwlock->writeno = 0; }
  rwlock->writeno++;
  pthread_cond_broadcast(&rwlock->c);
  return pthread_mutex_unlock(&rwlock->m);
}

// Atomically releases readlock and blocks until wrunlock is called, 
// reacquiring readlock before returning.  Similar to pthread_cond_wait.
int traft_rwlock_waitwrite(traft_rwlock_t *rwlock) {
  int err = pthread_mutex_lock(&rwlock->m);
  if (err != 0) { return err; }
  // Get current state ID
  int prev_write_id = rwlock->writeno;
  // Release readlock
  rwlock->num_readers--;
  // Wait until state changes
  while (rwlock->writeno == prev_write_id) {
    err = pthread_cond_wait(&rwlock->c, &rwlock->m);
    if (err != 0) { return err; }
  }
  // regain readlock
  rwlock->num_readers++;
  return pthread_mutex_unlock(&rwlock->m);
}
 