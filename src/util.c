#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

// Writes all.  Returns 0 on success, -1 on failure.
int traft_write_all(int fd, uint8_t *buf, size_t count) {
  while (count) {
    ssize_t w = write(fd, buf, count);
    if (w == -1) {
      return -1;
    }
    count -= w;
    buf += w;
  }
  return 0;
}

// Reads all.  Returns 0 on success, -1 on failure.
int traft_read_all(int fd, uint8_t *buf, size_t count) {
  while (count) {
    ssize_t r = read(fd, buf, count);
    if (r == -1) {
      return -1;
    }
    count -= r;
    buf += r;
  }
  return 0;
}

// Supports readlock, writelock, and wait_write functions.
// Holders of readlock can wait_write to atomically 
// release readlock and block until wrunlock is called.
typedef struct traft_rwlock_t {
  pthread_mutex_t m;
  pthread_cond_t  c;
  int             writer; // 1 if writer is waiting for or has acquired lock
  int             num_readers;    // current number of readers
  int             writeno; // Random number set after each write, used to wait on a change 
} traft_rwlock_t;

int traft_wrlock_init(traft_rwlock_t *rwlock) {
  rwlock->writer = 0;
  rwlock->num_readers = 0;
  rwlock->readers_waiting_write = 0;
  int err = pthread_mutex_init(&rwlock->m);
  if (err != 0) { return err; }
  return pthread_cond_init(&rwlock->c);
}

int traft_wrlock_destroy(traft_rwlock_t *rwlock) {
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
  rwlock->readers_waiting_write++; 
  pthread_cond_broadcast(&rwlock->c);
  return pthread_mutex_unlock(&rwlock->m);
}

// Atomically releases readlock and blocks until wrunlock is called, 
// reacquiring readlock before returning.  Similar to pthread_cond_wait.
int traft_rwlock_waitwrite(traft_rwlock_t *rwlock) {
  int err = pthread_mutex_lock(&rwlock->m);
  if (err != 0) { return err; }
  // Get current state ID
  int prev_write_id = rwlock->readers_waiting_write;
  // Release readlock
  rwlock->num_readers--;
  // Wait until state changes
  while (rwlock->readers_waiting_write == prev_write_id) {
    err = pthread_cond_wait(&rwlock->c, &rwlock->m);
    if (err != 0) { return err; }
  }
  // regain readlock
  rwlock->num_readers++;
  return pthread_mutex_unlock(&rwlock->m);
}
