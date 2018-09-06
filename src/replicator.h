
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
  traft_entry_id   last_sent;
  traft_entry_id   remote_committed; // last committed log_entry_id on this follower
  traft_entry_id   remote_applied;   // last applied traft_entry_id, i.e., this follower's opinion on quorum_committed
  log_set             *entries; 
  int                 stop; // if true, write thread will terminate
} logsender_state;



#define MAX_PEERS 15

typedef struct cluster_state {
  pthread_spinlock_t statelock;
  traft_entry_id  quorum_committed;
  logsender_state    senders[MAX_PEERS];
  pthread_t          sender_threads[MAX_PEERS];
  pthread_t          supervisor;
  int8_t             num_followers;
  int                stop; // gets set to >0 if we want to stop trying to replicate
} cluster_state;

/** Spins off threads replicating all data in this cluster */
void replicate_all(cluster_state *cluster);

/** Informs replicator it should stop within heartbeat_send_ms */
void stop_replicator(cluster_state *cluster);

/** Waits until replicator terminated. */
void join_replicator(cluster_state *cluster);
