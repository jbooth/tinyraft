#pragma once

#include <pthread.h>
#include "wiretypes.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct fwd_client {
  int               sock_fd;
  pthread_mutex_t   write_lock;  // guards writes to sock
  pthread_mutex_t   read_lock;   // guards reads and last_ack vars
  uint64_t          last_ack_write_term;
  uint32_t          last_ack_write_idx;
} fwd_client;

int forward_entries(struct iovec *args, int num_args);

#ifdef __cplusplus
}
#endif

