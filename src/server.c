#include <stddef.h>
#include <sys/uio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>

#include "tinyraft.h"
#include "buffers.h"
#include "wiretypes.h"
#include "storage.h"

#define MAX_CLIENTS 32

// Adds the client or returns -1 if we're already full
static int add_client(client_set *c, int fd, traft_clientinfo info) {
  pthread_spin_lock(&c->guard);
  if (c->count == MAX_CLIENTS) {
    pthread_spin_unlock(&c->guard);
    return -1;
  }
  c->fds[c->count] = fd;
  c->info[c->count] = info;
  c->count++;
  pthread_spin_unlock(&c->guard);
  return 0;
}

static int get_info(client_set *c, int fd, traft_clientinfo *info) {
  pthread_spin_lock(&c->guard);
  for (int i = 0 ; i < c->count ; i++) {
    if (c->fds[i] == fd) {
      memcpy(info, c->info[i], sizeof(traft_clientinfo));
      pthread_spin_unlock(&c->guard);
      return 0;
    }
  }
  // fd not found
  pthread_spin_unlock(&c->guard);
  return -1;
}

static void kill_client(client_set *c, int fd) {
  if (fd < 1) { return; }

  pthread_spin_lock(&c->guard);
  // find fd_idx in the array
  int fd_idx = -1;
  for (int i = 0 ; i < c->count ; i++) {
    if (c->fds[i] == fd) {
      fd_idx = i;
      break;
    }
  }
  // move top element to our last position and decrement count
  if (fd_idx >= 0) {
    int last_idx = c->count - 1;
    c->fds[fd_idx] = c->fds[last_idx];
    c->info[fd_idx] = c->info[last_idx];
    c->fds[last_idx] = -1;
    fds->count--;
  }
  pthread_spin_unlock(&c->guard);

  close(fd);
}

static void close_all(client_set *c) {
  // copy to temp buffer with lock held
  pthread_spin_lock(&c->guard);
  int fds[MAX_CLIENTS];
  int num_fds = c->count;
  for (int i = 0 ; i < c->count; i++) {
    fds[i] = c->fds[i];
    c->fds[i] = -1;
  }
  c->count = 0;
  pthread_spin_unlock(&c->guard);

  // close all
  for (int i = 0 ; i < num_fds ; i++) {
    if (fds[i]) { close(fds[i]); }
  }
}

// Sets up the provided array of pollfds and returns number of fds to poll for
static int prepare_poll(client_set *c, struct pollfd *fds) {
  memset(fds, 0, sizeof(pollfd) * MAX_CLIENTS);
  pthread_spin_lock(&c->guard);
  for (int i = 0 ; i < c->count ; i++) {
    fds[i].fd = c->fds[i];
    fds[i].events = POLLIN;
  }
  int count = c->count;
  pthread_spin_unlock(&c->guard);
  return count;
}

// Reads all.  Returns 0 on success, -1 on failure.
static int read_all(int fd, uint8_t *buf, size_t count) {
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

void raftlet_add_conn(raftlet_server* server, int client_fd, traft_hello *hello) {
  // decrypt session key
  char  session_key[32];
  if (crypto_box_curve25519xchacha20poly1305_open_detached(
    &session_key, &hello->session_key, &hello->mac, 32, &hello->nonce, &hello->client_id, &raftlet->private_key) == -1) {
    // decrypt error, tell client they're not auth'd and hangup
  }
  // add to client_set
  traft_clientinfo clientinfo;
  memcpy(&clientinfo.remote_id, &hello->client_id, 32);
  memcpy(&clientinfo.session_key, &hello->session_key, 32);
  add_client(&server->clients, client_fd, clientinfo);
}

void * traft_serve_raftlet(void *arg) {
  raftlet_server *server = (raftlet_server*) arg;
  struct pollfd poll_fds[MAX_CLIENTS];
  while (1) {
    int nfds = prepare_poll(&server->clients, &poll_fds);
    if (poll(&poll_fds, nfds, 100) == -1) {
      // poll error
    }
    for (int i = 0 ; i < nfds ; i++) {
      if (poll_fds[i].revents & (POLLHUP | POLLNVAL)) {
        // kill client
      }
      if (poll_fds[i].revents & POLLIN) {
      }
    }
  }
}

