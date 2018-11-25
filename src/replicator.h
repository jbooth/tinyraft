
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include "tinyraft.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct logsender {
  pthread_spinlock_t  statelock;
  traft_peer          follower_info;
  int                 follower_fd;
  int                 log_fd;
  int64_t             heartbeat_interval_ms;
  struct timeval      last_write_time;
  struct timeval      last_read_time;
  struct timeval      last_error_time;  // Set to 0 if running fine, set to time of last error if not
  traft_entry_id      last_sent;
  traft_entry_id      remote_committed; // last committed log_entry_id on this follower
  traft_entry_id      remote_quorum;    // last applied traft_entry_id, i.e., this follower's opinion on quorum_committed
  log_set             *entries; 
  pthread_t           thread;
  int8_t              initialized; // if true, we've had the above values set up
  int8_t              running;  // write thread will set this to false on termination
  int8_t              stop;     // if true, write thread will terminate when it reads this
  int8_t              exitcode; // -1 while running, 0 on requested stop, >0 on error stop
} logsender;

#define MAX_PEERS 15

typedef struct cluster_state {
  pthread_spinlock_t  statelock;
  traft_entry_id      quorum_committed;
  int64_t             heartbeat_interval_ms;
  logsender           senders[MAX_PEERS]; // compacted array of senders.  inactive senders are zeroed out.
  pthread_t           supervisor_thread;
  int                 epoll_fd;
  int                 log_fd;
  int8_t              num_senders; // number of running senders
  int8_t              num_followers;
  int8_t              stop;     // gets set to >0 if we want to stop trying to replicate
  int8_t              exitcode; // -1 while running, 0 on requested stop, >0 on error stop
} cluster_state;

/** Spins off threads replicating all data in this cluster */
void replicate_all(traft_peer *followers, int8_t num_followers, 
                    int64_t heartbeat_interval_ms, int log_fd,
                    cluster_state *cluster);

/** Informs replicator it should stop within heartbeat_send_ms */
void stop_replicator(cluster_state *cluster);

/** Waits until replicator terminated. */
void join_replicator(cluster_state *cluster);

#ifdef __cplusplus
}
#endif

