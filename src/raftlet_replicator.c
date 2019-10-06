#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "raftlet.h"

struct logsender {
  pthread_t               send_thread;
  traft_conninfo_t        conninfo;
  traft_entry_id          last_sent;
  traft_entry_id          last_acked;
  int                     fd;
};

struct replication_state {
  traft_raftlet_s     *raftlet;
  pthread_t           follower_resp_reader;
  pthread_t           state_updater;
  struct logsender    followers[TRAFT_MAX_PEERS];
};

static void * write_follower(void *arg) {
  struct logsender *sender = (struct logsender*) arg;
  
  // send initial heartbeat


  traft_entry_id to_send_start, to_send_end = (traft_entry_id) {.term_id = 0, .idx = 0};

  

  // re-used to send heartbeat reqs
  generic_req heartbeat_req = heartbeat_request();

  pthread_spin_lock(&sender->statelock);
  int follower_fd = sender->follower_fd;
  log_set *entries_store = sender->entries;
  int64_t heartbeat_interval_ms = sender->heartbeat_interval_ms;
  pthread_spin_unlock(&sender->statelock);

  while(1) {
    struct timeval now;

    if (to_send.count > 0) {
      // Entries are available, send them.
      if (send_entries(follower_fd, entries_store, &to_send) == -1) {
        mark_down(sender, "Error sending entries", errno);
        return NULL;
      }
      gettimeofday(&now, NULL);
      pthread_spin_lock(&sender->statelock);
      sender->last_write_time = now;
      pthread_spin_unlock(&sender->statelock);
    } else { 
      // Check if heartbeat is warranted.
      traft_entry_id quorum_entry_id = get_quorum_id(entries_store);
      pthread_spin_lock(&sender->statelock);
      struct timeval prev_write_time = sender->last_write_time;
      traft_entry_id follower_quorum_opinion = sender->remote_quorum;
      pthread_spin_unlock(&sender->statelock);

      gettimeofday(&now, NULL);
      int64_t time_since_heartbeat_ms = (now.tv_sec - prev_write_time.tv_sec) * 1000;
      time_since_heartbeat_ms += (now.tv_usec - prev_write_time.tv_usec) / 1000;

      // If heartbeat interval has elapsed or if we have new quorum info to send, send heartbeat
      if (time_since_heartbeat_ms >= heartbeat_interval_ms
          || (follower_quorum_opinion.term_id < quorum_entry_id.term_id
                || follower_quorum_opinion.idx < quorum_entry_id.idx)) {
        heartbeat_req.message.append_entries.quorum_term = quorum_entry_id.term_id;
        heartbeat_req.message.append_entries.quorum_idx = quorum_entry_id.idx;
        int err = send_req_raw(follower_fd, &heartbeat_req);
        if (err == -1) {
          mark_down(sender, "Error sending heartbeat", errno);
          return NULL;
        }
        gettimeofday(&now, NULL);
        pthread_spin_lock(&sender->statelock);
        sender->last_write_time = now;
        pthread_spin_unlock(&sender->statelock);

      } else {
        // Block for more entries, up until our next deadline for heartbeat.
        int64_t sleep_time_ms = heartbeat_interval_ms - time_since_heartbeat_ms;
        // TODO limit this to max inflight
        wait_more(entries_store, &to_send, 16, sleep_time_ms);
      }
    }
  } // while(1)
}

static void get_next_to_send(struct follower_repl_state *follower_state, traft_entry_id *next_to_send);

static int is_later(traft_entry_id later, traft_entry_id sooner) {
  return (later.term_id > sooner.term_id || (later.term_id == sooner.term_id && later.idx > sooner.idx));
};

/** Reads last_sent and sends up to quorum_entry, re-setting last_sent in the process */
static int follower_send_some(struct follower_repl_state *follower, traft_entry_id quorum_entry) {
  pthread_mutex_lock(&follower->r_set->guard);
  traft_entry_id last_sent = follower->last_sent;
  traft_raftlet *raftlet = follower->r_set->raftlet;
  int send_fd = follower->fd;
  send_queue to_send;
  memset(&to_send, 0, sizeof(send_queue));
  pthread_mutex_unlock(&follower->r_set->guard);

  int err = traft_raftlet_send_to(raftlet, send_fd, &last_sent, quorum_entry);
  if (err != 0) { return err; }

  pthread_mutex_lock(&follower->r_set->guard);
  follower->last_sent = last_sent;
  pthread_mutex_unlock(&follower->r_set->guard);
}

static void* send_follower(void *arg) {
  struct follower_repl_state *state = (struct follower_repl_state*)arg;
  
  pthread_mutex_lock(&state->r_set->guard);
  int send_fd = state->fd;
  traft_raftlet *raftlet = state->r_set->raftlet;
  pthread_mutex_unlock(&state->r_set->guard);

  traft_entry_id quorum_commit = { .term_id = 0, .idx = 0 };
  traft_entry_id local_commit = {.term_id = 0, .idx = 0 };
  traft_entry_id last_sent = {.term_id = 0, .idx = 0 };

  // TODO send hello and initial request, read response

  // TODO schedule FD with response_reader

  

  while (traft_raftlet_waitnext(raftlet, &quorum_commit, &local_commit, /* max_wait_ms= */ 200) == 0) {
    // Send heartbeat
    traft_appendentry_req heartbeat = {
      .this_term = 0,
      .this_idx = 0,
      .info.body_len = 0,
      .prev_term = last_sent.term_id,
      .prev_idx = last_sent.idx,
      .quorum_term = quorum_commit.term_id,
      .quorum_idx = quorum_commit.idx,
    };
    // Send entries if we're ahead
    while (is_later(local_commit, last_sent)) {
      if (traft_raftlet_send_messages(&last_sent, &local_commit, send_fd) != 0) {
        goto SEND_FAIL;
      }
      pthread_mutex_lock(&state->r_set->guard);
      state->last_sent = last_sent;
      pthread_mutex_unlock(&state->r_set->guard);
    }
  }
  SEND_FAIL:
  return NULL;
}

void * traft_replicate(void *arg) {
  traft_raftlet *raftlet = (traft_raftlet*) arg;
  replication_set *r_set = malloc(sizeof(replication_set))
  
  while (1) {
    // check configs

    // ensure all threads running
  }
}

static void mark_down(logsender *sender, char *message, int8_t err_no) {
  pthread_spin_lock(&sender->statelock);
  close(sender->follower_fd);
  // TODO add remote addr to log statement
  sender->follower_fd = 0;
  sender->exitcode = err_no;
  char *err_str = strerror(err_no);
  dprintf(sender->log_fd, "%s : %s", message, err_str);
  gettimeofday(&sender->last_error_time, NULL);
  pthread_spin_unlock(&sender->statelock);
}




// Connects a sender and initializes fields
// Expects sender->follower_info already populated
static int connect_sender(logsender *sender) {
  pthread_spin_init(&sender->statelock, 0);
  pthread_spin_lock(&sender->statelock);
  // TODO infer domain from addr instead of assuming ipv4
  sender->follower_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (sender->follower_fd == -1) {
    // DIE
  }
  int err = connect(sender->follower_fd, &sender->follower_info.addr, sender->follower_info.addrlen);
  if (err < 0) { 
    // DIE 
  }
  return 0;
}

/* Updates sender's state with the new information received 
 * from follower in a heartbeat response  */
static inline int update_sender(logsender *sender, generic_resp *resp) {
  int err = pthread_spin_lock(&sender->statelock);
  sender->remote_committed.term_id = resp->message.append_entries.committed_term;
  sender->remote_committed.idx     = resp->message.append_entries.committed_idx;
  sender->remote_quorum.term_id = resp->message.append_entries.quorum_term;
  sender->remote_quorum.idx     = resp->message.append_entries.quorum_idx;
  err = err | pthread_spin_lock(&sender->statelock);
  return err;
}

// Sends heartbeat and dispatches sender thread
// Sender thread will sleep until we've received
// at least one heartbeat response.
static int init_sender(logsender *sender) {
  return 0;
}

// Multi-step process to init each sender:
// 1)  Set up socket
// 2)  Register for EPOLLIN
// 3)  Initial heartbeat to establish replication state
// 4)  Dispatch thread to send entries
static int init_all_senders(cluster_state *cluster) {
  int i, err;
  // Connect, send heartbeats, register for epoll
  generic_req heartbeat_req = heartbeat_request();

  int epoll_fd = epoll_create(MAX_PEERS);
  if (epoll_fd < 0) {
    return epoll_fd;
  }
  cluster->epoll_fd = epoll_fd;
  for (i = 0 ; i < cluster->num_followers ; i++) {
    err = connect_sender(&cluster->senders[i]);
    if (err == -1) {
      mark_down(&cluster->senders[i], "Error connecting", errno);
      // Mark failed
    }
    int follower_fd = cluster->senders[i].follower_fd;
    struct epoll_event epoll_ev;
    epoll_ev.events = EPOLLIN;
    epoll_ev.data.fd = follower_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, follower_fd, &epoll_ev) == -1) {
      // TODO epoll isn't working, bail on everything
    }
  }
  // Recv heartbeats
  generic_resp resp;
  for (i = 0 ; i < cluster->num_followers ; i++) {
    err = read_resp_raw(cluster->senders[i].follower_fd, &resp); 
    if (err != 0) {
      // TODO handle follower down
      // TODO this could time out
    }
    update_sender(&cluster->senders[i], &resp);
  }

  // Start threads
  for (i = 0 ; i < cluster->num_followers ; i++) {
    err = pthread_create(&cluster->senders[i].thread, NULL, &write_follower, (void*) &cluster->senders[i]);
    if (err != 0) {
      // TODO system level error, should clean up and bail from everything
    }
  }
  return 0;
}

static int restart_sender(logsender *sender) {
  return 0;
}

static logsender * get_sender_for_fd(int fd, cluster_state *cluster) {
  int8_t i;
  for (i = 0 ; i < cluster->num_followers ; i++) {
    if (cluster->senders[i].follower_fd = fd) {
      return &cluster->senders[i];
    }
  }
  return NULL;
}

static void * do_replicate_all(void *arg) {
  cluster_state *cluster = (cluster_state*)arg;
  generic_resp resp;

  pthread_spin_lock(&cluster->statelock);
  int64_t heartbeat_interval_ms = cluster->heartbeat_interval_ms;
  int err = init_all_senders(cluster);
  pthread_spin_unlock(&cluster->statelock);

  if (err != 0) {
    // TODO shutdown all
  }
  struct epoll_event events[MAX_PEERS];
  while (1) {
    // Check any readable and update sender status
    int num_ready = epoll_wait(cluster->epoll_fd, events, MAX_PEERS, heartbeat_interval_ms);
    if (num_ready == -1) {
      // TODO die
    }
    for (int i = 0 ; i < num_ready ; i++) {
      int follower_fd = events[i].data.fd;
      pthread_spin_lock(&cluster->statelock);
      logsender *sender = get_sender_for_fd(follower_fd, cluster);
      pthread_spin_unlock(&cluster->statelock);
      if (sender == NULL) {
        // TODO log something
        close(follower_fd);
        continue;
      }
      int err = read_resp_raw(follower_fd, &resp);
      if (err == -1) {
        mark_down(sender, "error reading response", errno);
        continue;
        // TODO kill sender
      }

      update_sender(sender, &resp);
    }

    // Check if any threads should be relaunched

  }
  STOP_ALL:

  return NULL;
}

void replicate_all(traft_peer *followers, int8_t num_followers, 
                    int64_t heartbeat_interval_ms, int log_fd,
                    cluster_state *cluster) {
  // Lightweight initialization and then dispatch thread for the rest.
  // Initializes:  statelock, heartbeat_interval_ms, num_followers, and the follower_addr field on each sender
  // Further initialization requiring syscalls happens in the dedicated thread
  memset(cluster, 0, sizeof(cluster_state));
  int err = pthread_spin_init(&cluster->statelock, 0);
  err = pthread_spin_lock(&cluster->statelock);
  cluster->num_followers = num_followers;
  cluster->heartbeat_interval_ms = heartbeat_interval_ms;
  for (int i = 0 ; i++ ; i < num_followers) {
    cluster->senders[i].follower_info = followers[i];
    cluster->senders[i].heartbeat_interval_ms = heartbeat_interval_ms;
  }
  err = pthread_spin_unlock(&cluster->statelock);
  // dispatch actual thread
  err = pthread_create(&cluster->supervisor_thread, NULL,
    &do_replicate_all, (void*)cluster); 
}
