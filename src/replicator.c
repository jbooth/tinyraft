#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "replicator.h"
#include "log_store.h"


static int connect_client(const struct sockaddr *addr, socklen_t addrlen) {
  // TODO infer domain from addr instead of assuming ipv4
  int client_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  int err = connect(client_fd, addr, addrlen);
  if (err < 0) { return err; }
  return client_fd;
}

static int read_resp(int follower_fd, generic_resp *resp) {
  generic_resp new_resp;
  char *buf = (char*)&new_resp;
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


static void * write_follower(logsender_state *state) {
  // manages state of pending file transfers
  send_queue to_send;
  memset(&to_send, 0, sizeof(send_queue));

  // re-used to send heartbeat reqs
  generic_req heartbeat_req;
  heartbeat_req.info.opcode = OPCODE_append_entries;
  heartbeat_req.message.append_entries.this_term = 0;
  heartbeat_req.message.append_entries.this_idx = 0;

  pthread_spin_lock(&state->statelock);
  int64_t heartbeat_interval_ms = 100; //state->heartbeat_rate_ms;
  int follower_fd = state->follower_fd;
  log_set *entries_store = state->entries;
  pthread_spin_unlock(&state->statelock);

  while(1) {
    struct timeval now;
    gettimeofday(&now, NULL);

    if (to_send.count > 0) {
      // Entries are available, send them.
      if (send_entries(follower_fd, entries_store, &to_send) == -1) {
        goto DIE;
      }
      pthread_spin_lock(&state->statelock);
      gettimeofday(&now, NULL);
      state->last_write_time = now;
      pthread_spin_unlock(&state->statelock);
    } else { 
      // Check if heartbeat is warranted.
      traft_entry_id quorum_entry_id = get_quorum_id(entries_store);
      pthread_spin_lock(&state->statelock);
      struct timeval prev_write_time = state->last_write_time;
      traft_entry_id follower_quorum_opinion = state->remote_applied;
      pthread_spin_unlock(&state->statelock);

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
          goto DIE;
        }
        pthread_spin_lock(&state->statelock);
        state->last_write_time = now;
        pthread_spin_unlock(&state->statelock);

      } else {
        // Block for more entries, up until our next deadline for heartbeat.
        int64_t sleep_time_ms = heartbeat_interval_ms - time_since_heartbeat_ms;
        // TODO limit this to max inflight
        wait_more(entries_store, &to_send, 16, sleep_time_ms);
      }
    }
  } // while(1)
  DIE:
    close(follower_fd);
    return NULL;
}
