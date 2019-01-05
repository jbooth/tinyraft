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

// track fds so we can make sure all are closed
struct client_set {
  int              fds[MAX_CLIENTS];
  traft_clientinfo info[MAX_CLIENTS];
  int count;
};

// Adds the client or returns -1 if we're already full
static int add_client(struct client_set *c, int fd, traft_clientinfo info) {
  if (c->count == MAX_CLIENTS) {
    return -1;
  }
  c->fds[c->count] = fd;
  c->info[c->count] = info;
  c->count++;
  return 0;
}

static int get_info(struct client_set *c, int fd, traft_clientinfo *info) {
  for (int i = 0 ; i < c->count ; i++) {
    if (c->fds[i] == fd) {
      memcpy(info, c->info[i], sizeof(traft_clientinfo));
      return 0;
    }
  }
  // fd not found
  return -1;
}

static void kill_client(struct client_set *c, int fd) {
  if (fd) { close(fd); }

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
}

static void close_all(struct client_set *c) {
  for (int i = 0 ; i < c->count ; i++) {
    int fd = c->fds[i];
    if (fd) {
      close(fd);
    }
    c->fds[i] = -1;
  }
  c->count = 0;
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

static int read_hello(int fd, traft_hello *msg, uint8_t *private_key) {
  if (read_all(fd, (uint8_t*) msg, RPC_HELLO_LEN) == -1) {
    // read err
    return -1;
  }
  if (crypto_box_open_detached(&msg->session_key, 
}

void * traft_serve(void *arg) {
  traft_server_s *server = (traft_server_s*) arg;
  
  // accept conns and delegate to raftlets
  struct sockaddr new_conn_addr;
  socklen_t new_conn_addrlen;
  traft_hello hello;;  // msg header + body
  while (1) {
    int client_fd = accept(server->accept_fd, &new_conn_addr, &new_conn_addrlen);
    if (client_fd == -1) {
      // accept socket is bad, die somehow
    }
    // read hello
    if (read_all(client_fd, (uint8_t*) &hello, RPC_HELLO_LEN) == -1) {
      // kill conn
    }
    // locate raftlet for this cluster_id
    
  
    // authenticate, confirm we have raftlet
    // write hello_resp
    // assign to raftlet 
  }
}

int traft_start_server(uint16_t port, traft_server *ptr) {
  // allocate server
  traft_server *server = malloc(sizeof(traft_server_s));
  memset(server, 0, sizeof(traft_server_s));
  server->bind_port = cfg->port;
  pthread_mutex_init(&server->raflets_guard);

  // bind socket to all IPv4 incoming traffic for port
  server->accept_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (server->accept_fd < 0) {
    free(server);
    return -1;
  }

  memset(&server->accept_addr, 0, sizeof(struct sockaddr_in));
  server->accept_addr.sin_port = port;
  server->accept_addr.sin_addr = INADDR_ANY;
  if (bind(server->accept_fd, &server->accept_addr, sizeof(struct sockaddr_in) == -1) {
    free(server);
    return -1;
  }

  // start thread to accept conns
  if (pthread_create(&server->accept_thread, NULL, &traft_serve, server) == -1) {
    free(server);
    return -1;
  }
  *ptr = server;
  return 0;
}

