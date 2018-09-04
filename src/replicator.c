#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "replicator.h"
#include "log_store.h"


/* Brings a follower up to date with the provided term and idx. Modifies follower_state as it works. */
static int update_follower(
    replicator_state *replicator, 
    append_entries_resp *follower_state, 
    log_entry_id desired_state) {
    // while follower state 
    return 0;
}
/* Issues a heartbeat (0-content) entry and fills follower_state with the response. */
static int heartbeat(replicator_state *replicator, append_entries_resp *follower_state) {
    // sends heartbeat, blocks until return
    return 0;
}

static int follower_needs_entries(append_entries_resp *follower_state, log_state *current_log_state) {
    return 0;
}

static int follower_needs_quorum_update(append_entries_resp *follower_state, log_state *current_log_state) {
    return 0;
}

static int connect_client(const struct sockaddr *addr, socklen_t addrlen) {
  // TODO infer domain from addr instead of assuming ipv4
  int client_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  int err = connect(client_fd, addr, addrlen);
  if (err < 0) { return err; }
  return client_fd;
}

static int register_epoll(int client_fd) {
  struct epoll_event ev;

}

static generic_req heartbeat_req() {
    append_entries_req heartbeat;
    heartbeat.is_heartbeat = 1;
    generic_req heartbeat_req;
    heartbeat_req.message.append_entries = heartbeat;
    return heartbeat_req;
}

/* Represents bytes in the logfile that we want to send when we can. */
typedef struct bytes_to_send {
  uint64_t pos;
  uint64_t count;
  log_id last_entry_queued;
} bytes_to_send;

static void update_to_send(
  append_entries_resp *follower_state,
  log_store           *leader_log,
  bytes_to_send       *to_send) {
}


static int read_resp(int follower_fd, generic_resp *resp) {
  generic_resp new_resp;
  char *buf = &new_resp;
  ssize_t cnt = RPC_RESP_LEN;

  // 32 byte value should have come in a single packet.
  // Try to read in a single shot and return EAGAIN if no bytes available.
  ssize_t r = read(follower_fd, buf, cnt);

  if (r == -1) { return -1; }

  if (r == RPC_RESP_LEN) { return 0; }

  // Partially filled somehow
  // TODO deadline
  while (cnt) {
    ssize_t r = read(follower_fd, buf, cnt);
    if (r == -1) { 
      if (errno == EAGAIN) {
        continue;
      } else {
        return -1;
      }
    }
    cnt -= r;
    buf += r;
  }
  return 0;
}


static void * write_follower(follower_state *follower) {
  // manages state of pending file transfers
  send_queue to_send;
  memset(to_send, 0, sizeof(send_queue));

  // re-used to send heartbeat reqs
  generic_req heartbeat_req;
  heartbeat_req.req_info.opcode = OPCODE_append_entries;
  heartbeat_req.message.append_entries.this_term = 0;
  heartbeat_req.message.append_entries.this_idx = 0;

  thread_spinlock_lock(follower->statelock);
  int64_t heartbeat_interval_ms = follower->heartbeat_rate_ms;
  int follower_fd = follower->follower_fd;
  log_set *entries_store = follower->entries;
  pthread_spinlock_unlock(follower->statelock);

  while(1) {
    struct timeval now;
    gettimeofday(&now, NULL);

    if (to_send.count > 0) {
      // Entries are available, send them.
      if (send_entries(follower_fd, entries_store, &to_send) == -1) {
        goto DIE;
      }
      pthread_spinlock_lock(follower->statelock);
      gettimeofday(&now, NULL);
      follower->last_write_time = now;
      pthread_spinlock_unlock(follower->statelock);
    } else { 
      // Check if heartbeat is warranted.
      log_entry_id quorum_entry_id = get_quorum_id(entries_store);
      pthread_spinlock_lock(&follower->statelock);
      struct timeval prev_write_time = *follower->last_write_time;
      log_entry_id follower_quorum_opinion = *follower->remote_applied;
      pthread_spinlock_unlock(&follower->statelock);

      int64_t time_since_heartbeat_ms = (now.tv_sec - prev_write_time.tv_sec) * 1000;
      time_since_heartbeat_ms += (now.tv_usec - prev_write_time.tv_usec) / 1000;

      // If heartbeat interval has elapsed or if we have new quorum info to send, send heartbeat
      if (time_since_heartbeat_ms >= heartbeat_interval_ms
          || (follower_quorum_opinion.term_id < quorum_entry_id.term_id
                || follower_quorum_opinion.idx < quorum_entry_id.idx)) {
        heartbeat_req.append_entries.quorum_term = quorum_entry_id.term_id;
        heartbeat_req.append_entries.quorum_idx = quorum_entry_id.idx;
        int err = send_req_raw(follower_fd, &heartbeat_req);
        if (err == -1) {
          goto DIE;
        }
        pthread_spinlock_lock(follower->statelock);
        follower->last_write_time = now;
        pthread_spinlock_unlock(follower->statelock);

      } else {
        // Block for more entries, up until our next deadline for heartbeat.
        int64_t sleep_time_ms = heartbeat_interval_ms - time_since_heartbeat_ms;
        // TODO limit this to max inflight
        wait_more(entries_store, &to_send, 16, sleep_time_ms);
      }
    }
  } // while(1)
  DIE:
    close(follower->follower_fd);
    return NULL;
}

#define MAX_INFLIGHT 16

int replicate(follower_state *follower) {
    // state we use to decide what to do
    generic_resp last_resp;
    mem_set(&last_resp, 0, sizeof(generic_resp));
    append_entries_resp *current_follower_state = &heartbeat_resp.append_entries;
    log_state current_log_state;
    struct timeval last_heartbeat_time;
    bytes_to_send to_send;    
    mem_set(&to_send, 0, sizeof(bytes_to_send));

    // make connection and register for polling
    int client_fd = connect_client();
    if (client_fd < 0) { goto DIE; }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) { goto DIE; }
    struct epoll_event epoll_ev;
    epoll_ev.events = EPOLLIN | EPOLLOUT;
    epoll_ev.data.fd = client_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &epoll_ev) == -1) { goto DIE; }

    // re-use this for heartbeat requests
    generic_req heartbeat_req = heartbeat_req();
    
    // initial log state
    log_state current_log_state = get_log_state(replicator->logs);

    // send initial round-trip to establish follower state
    int err = send_req_raw(client_fd, &heartbeat_req);
    if (err < 0) { goto DIE; }
    err = read_resp_raw(client_fd, &last_resp);
    if (err < 0) { goto DIE; }
    gettimeofday(&last_heartbeat_time, NULL);

    int poll_interval = 100;

    uint32_t readable, writable = 0;

    while (1) {

        if (! readable || ! writable) {
          int err = epoll_wait(epoll_fd, &epoll_ev, 1, 0);
          if (err == -1) { goto DIE; }
          readable = readable | (epoll_ev.event & EPOLLIN);
          writable = writable | (epoll_ev.event & EPOLLOUT);
        }

        // Pull all responses that have arrived
        while (readable) {
          int err = read_resp(client_fd, &current_follower_state);
          if (err == -1) {
            if (errno == EAGAIN) { 
              readable = 0;
              break;
            } else {
              // real error
              goto DIE;
            }
          }
        }

        // Check if we can extend our queue

        // Send if there's anything to send
        while (writable & to_send.count) {
        }

        // Wait for more if applicable
        if (writable && to_send.count == 0) {
        }

        

        
        if (num_requests_inflight == 0 ) // && client_up_todate()
           

        // poll read/write for heartbeat_interval / 2
        // if readable, read one
        // if nothing to do, block on new logs for heartbeat interval / 2
        // if follower needs more logs and < MAX_INFLIGHT, send one
        // else if follower needs status update, send that
        // 
    }

    DIE:
        close(client_fd);
        close(epoll_fd);

    // get current max log index
    // send empty RPC to get current follower index
    
    // while client_state = max_index, block on new entries
        //  epoll read/write on client
        //  if readable, pull responses and update client_state
        //  send new up to MAX_INFLIGHT
        //  reloop epoll
        //  if client_state = max_index break and reblock on log
    
    return 0;
}

int replicate_all(cluster_replicator *cluster) {
    return 0;
}
