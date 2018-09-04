
#pragma once
#include <pthread.h>
#include <stdint.h>
#include "tinyraft.h"
#include "rpc.h"
#include "log_store.h"

typedef struct logsender_state {
  pthread_spinlock_t  statelock;
  int                 follower_fd;
  int64_t             heartbeat_interval_ms;
  struct timeval      last_write_time;
  struct timeval      last_read_time;
  log_entry_id        last_sent;
  log_entry_id        remote_committed; // last committed log_entry_id on this follower
  log_entry_id        remote_applied;   // last applied log_entry_id, i.e., this follower's opinion on quorum_committed
  log_set             *entries; 
  cluster_state       *cluster;
  int                 stop; // if true, write thread will terminate
} follower_state;



typedef struct cluster_state {
  pthread_spinlock_t statelock;
  log_entry_id       quorum_committed;
  follower_state     followers[MAX_PEERS];
  pthread_t          follower_writers[MAX_PEERS];
  pthread_t          follower_readers[MAX_PEERS];
  int8_t             num_followers;
  int                stop; // gets set to >0 if we want to stop trying to replicate
} cluster_state;

/** Spins off threads replicating all data in this cluster */
void replicate_all(cluster_state *cluster);

/** Informs replicator it should stop within heartbeat_send_ms */
void stop_replicator(cluster_state *cluster);

/** Waits until replicator terminated. */
void join_replicator(cluster_state *cluster);
