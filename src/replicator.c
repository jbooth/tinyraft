#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "replicator.h"
#include "storage.h"


static generic_req heartbeat_request() {
  generic_req heartbeat_req;
  heartbeat_req.info.opcode = OPCODE_append_entries;
  heartbeat_req.message.append_entries.this_term = 0;
  heartbeat_req.message.append_entries.this_idx = 0;
	return heartbeat_req;
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

static void * write_follower(void *arg) {
  logsender *sender = (logsender*) arg;
  // manages state of pending file transfers
  send_queue to_send;
  memset(&to_send, 0, sizeof(send_queue));

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
      int err = read_resp_raw(follower_fd, &resp);
      pthread_spin_lock(&cluster->statelock);
      if (err == -1) {
        close(follower_fd);
        // TODO kill sender
      }
                  
      logsender *sender = get_sender_for_fd(follower_fd, cluster);
      if (sender == NULL) {
        close(follower_fd);
        // TODO kill sender
      } else {
        update_sender(sender, &resp);
      }
      pthread_spin_unlock(&cluster->statelock);
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
