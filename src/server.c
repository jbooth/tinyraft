#include <stddef.h>
#include <sys/uio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <poll.h>
#include <linux/in.h>

#include "buffers.h"
#include "wiretypes.h"
#include "raftlet.h"
#include "server.h"

/*
 * This file manages two concepts:
 * 1)  A servlet_s, which manages a single raftlet and all connections which have dialed through to it.
 * 2)  A server_s, which binds to a port, listens for new connections and authenticates them with registered raftlets.
 */
#define MAX_CLIENTS 64

// Tracks the set of clients being listened to by a server
typedef struct traft_client_set {
  pthread_mutex_t  guard;
  int                 fds[MAX_CLIENTS];
  traft_clientinfo    info[MAX_CLIENTS];
  int                 count;
} traft_client_set;

// Represents the server for a single raftlet
typedef struct traft_servlet_s {
  traft_servlet_s   *next;     // intrusive linked list
  traft_client_set  clients;
  traft_raftlet_s   raftlet;
  traft_server_ops  ops;
} traft_servlet_s;

// Adds the client or returns -1 if we're already full
static int servlet_add_client(traft_client_set *c, int fd, traft_clientinfo info) {
  pthread_mutex_lock(&c->guard);
  if (c->count == MAX_CLIENTS) {
    pthread_mutex_unlock(&c->guard);
    return -1;
  }
  c->fds[c->count] = fd;
  c->info[c->count] = info;
  c->count++;
  pthread_mutex_unlock(&c->guard);
  return 0;
}

static int servlet_get_clientinfo(traft_client_set *c, int fd, traft_clientinfo *info) {
  pthread_mutex_lock(&c->guard);
  for (int i = 0 ; i < c->count ; i++) {
    if (c->fds[i] == fd) {
      *info = c->info[i];
      pthread_mutex_unlock(&c->guard);
      return 0;
    }
  }
  // fd not found
  pthread_mutex_unlock(&c->guard);
  return -1;
}

static void servlet_kill_client(traft_client_set *c, int fd) {
  if (fd < 1) { return; }

  pthread_mutex_lock(&c->guard);
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
    c->count--;
  }
  pthread_mutex_unlock(&c->guard);

  close(fd);
}

static void servlet_close_all(traft_client_set *c) {
  // copy to temp buffer with lock held
  pthread_mutex_lock(&c->guard);
  int fds[MAX_CLIENTS];
  int num_fds = c->count;
  for (int i = 0 ; i < c->count; i++) {
    fds[i] = c->fds[i];
    c->fds[i] = -1;
  }
  c->count = 0;
  pthread_mutex_unlock(&c->guard);

  // close all
  for (int i = 0 ; i < num_fds ; i++) {
    if (fds[i]) { close(fds[i]); }
  }
}

// Sets up the provided array of pollfds and returns number of fds to poll for
static int servlet_prepare_poll(traft_client_set *c, struct pollfd *fds) {
  memset(fds, 0, sizeof(int) * MAX_CLIENTS);
  pthread_mutex_lock(&c->guard);
  for (int i = 0 ; i < c->count ; i++) {
    fds[i].fd = c->fds[i];
  }
  int count = c->count;
  pthread_mutex_unlock(&c->guard);
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

static void *servlet_run(void *arg) {
  traft_servlet_s *servlet = (traft_servlet_s*)arg;

  traft_buff req_buff;
  traft_resp resp;
  int err = traft_buff_alloc(&req_buff, 1024 * 1024);

  struct pollfd pollfds[MAX_CLIENTS];
  int polltimeout_ms = 200;
  traft_clientinfo info;
  
  while (1) {
    // poll
    int num_poll_fds = servlet_prepare_poll(&servlet->clients, pollfds);
    err = poll(pollfds, num_poll_fds, polltimeout_ms);
    if (err == -1) { 
      goto SERVLET_DIE;
    }
    for (int i = 0 ; i < num_poll_fds ; i++) {
      if (pollfds[i].revents & (POLLHUP | POLLERR)) {
        servlet_kill_client(&servlet->clients, pollfds[i].fd);
        continue;
      }
      if (pollfds[i].revents & POLLIN) {
        servlet_get_clientinfo(&servlet->clients, pollfds[i].fd, &info);
        // TODO timeout
        traft_buff_readreq(&req_buff, pollfds[i].fd);
        int err = servlet->ops.handle_request(&servlet->raftlet, &req_buff, &resp);
        if (err == -1) {
          goto SERVLET_DIE;
        }
        // TODO timeout
        traft_write_resp(&resp, pollfds[i].fd);
      }
    }
  }
  SERVLET_DIE:
  traft_buff_free(&req_buff);
  servlet_close_all(&servlet->clients);
  servlet->ops.destroy_raftlet(&servlet->raftlet);
}

static void servlet_add_conn(traft_servlet_s* server, int client_fd, traft_hello *hello) {
  if (traft_buff_decrypthello(hello, server->raftlet.private_key) != 0) {
    // TODO decrypt error, tell client they're not auth'd and hangup
  }
  // add to client_set
  traft_clientinfo clientinfo;
  memcpy(&clientinfo.remote_id, &hello->client_id, 32);
  memcpy(&clientinfo.session_key, &hello->session_key, 32);
  servlet_add_client(&server->clients, client_fd, clientinfo);
}

#define MAX_SERVLETS 256

typedef enum accepter_state {
  INIT, RUN, STOP_REQUESTED, DEAD  
} accepter_state;

/** Structure containing our accepter socket and all registered raftlets */
typedef struct traft_accepter_s {
  uint16_t            accept_port;
  traft_server_ops    ops;
  int                 accept_fd;
  pthread_t           accept_thread;
  pthread_mutex_t     servlets_guard;
  pthread_cond_t      state_change;
  accepter_state      state;

  traft_servlet_s     servlets[MAX_SERVLETS];
  int                 num_raftlets;
} traft_accepter_s;

static void wait_accepter_state(traft_accepter_s *accepter, accepter_state state) {
  pthread_mutex_lock(&accepter->servlets_guard);
  while (accepter->state != state) {
    pthread_cond_wait(&accepter->state_change, &accepter->servlets_guard);
  }
  pthread_mutex_unlock(&accepter->servlets_guard);
}

static void * traft_do_accept(void *arg) {
  traft_accepter_s *server = (traft_accepter_s*) arg;
  // set up server
  pthread_mutex_lock(&server->servlets_guard);


  pthread_mutex_unlock(&server->servlets_guard);
  // accept conns and delegate to raftlets
  struct sockaddr new_conn_addr;
  socklen_t new_conn_addrlen;
  traft_hello hello;;  // msg header + body
  while (1) {
    int client_fd = accept(server->accept_fd, &new_conn_addr, &new_conn_addrlen);
    if (client_fd == -1) {
      // TODO detect if this is recoverable
      goto ACCEPTER_DIE;
    }
    // read hello message
    // TODO this should have a somewhat aggressive timeout, these connections aren't authenticated yet
    // TODO TODO DDOS vulnerability
    if (read_all(client_fd, (uint8_t*) &hello, RPC_HELLO_LEN) == -1) {
      // kill conn
      close(client_fd);
      continue;
    }
    // locate raftlet for this cluster_id
    traft_raftlet_s *found_raftlet = NULL;
    
    for (traft_servlet_s *servlet = server->servlets ; servlet = servlet->next ; servlet->next != NULL) {
      traft_raftlet_s *raftlet = &servlet->raftlet;
      if (memcmp(&hello.cluster_id, raftlet->cluster_id, 16) == 0 
          && memcmp(&hello.server_id, raftlet->raftlet_id, 32) == 0) {
        found_raftlet = raftlet;
        break;
      }
    }
    // No raftlet for this ID, kill..  should we send an error back to client?
    // TODO log
    if (found_raftlet == NULL) { close(client_fd); continue; }
    // process raftlet
    servlet_add_conn(found_raftlet, client_fd, &hello);
  }
  ACCEPTER_DIE:
  close(server->accept_fd);
  // TODO close all servlets/raftlets
  // TODO log errno
  free(server);
}

static int bind_accepter_sock(traft_accepter_s *server) {
  // TODO either bind to specific addr or bind 2 FDs for inet4 and inet6
  server->accept_fd = socket(AF_INET, SOCK_STREAM, SOCK_CLOEXEC);
  if (server->accept_fd == -1) { return -1; }
  struct sockaddr_in in_any;
  in_any.sin_family = AF_INET;
  in_any.sin_addr.s_addr = INADDR_ANY;
  in_any.sin_port = server->accept_port;
  int err = bind(server->accept_fd, &in_any, sizeof(struct sockaddr_in));
  if (err == -1) { return -1; } 
  return listen(server->accept_fd, 10);
}

int traft_srv_start_server(uint16_t port, traft_server *ptr, traft_server_ops ops) {
  traft_accepter_s *server = malloc(sizeof(traft_accepter_s));
  if (server == NULL) {
    return -1;
  }
  server->accept_port = port;
  server->ops = ops;
  pthread_mutex_init(&server->servlets_guard, NULL);
  pthread_cond_init(&server->state_change, NULL);
  server->state = INIT;
  int err = bind_accepter_sock(server);
  if (err == -1) { goto START_SERVER_ERR; }
  memset(server->servlets, 0, sizeof(traft_servlet_s) * MAX_SERVLETS);
  err = pthread_create(&server->accept_thread, NULL, &traft_do_accept, server);
  if (err != 0) { goto START_SERVER_ERR; }
  // wait until serving
  wait_accepter_state(server, RUN);
  *ptr = server;
  return 0;

  START_SERVER_ERR:
  if (server->accept_fd > 0) { close(server->accept_fd); }
  free(server);
  return -1;
}


int traft_stop_server(traft_server server_ptr) {
  traft_accepter_s *server = (traft_accepter_s*) server_ptr;
  // mark to die
  pthread_mutex_lock(&server->servlets_guard);
  server->state = STOP_REQUESTED;
  // wait dead
  while (server->state != DEAD) {
    pthread_cond_wait(&server->state_change, &server->servlets_guard);
  }
  // clean up
  close(server->accept_fd);
  free(server);
}

int traft_run_raftlet(const char *storagepath, traft_server server, traft_statemachine_ops ops, 
void *state_machine, traft_raftlet *raftlet) {
  return 0;
                      
}

int traft_stop_raftlet(traft_raftlet *raftlet);

int traft_join_raftlet(traft_raftlet *raftlet);


