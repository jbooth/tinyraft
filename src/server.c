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
#include "server.h"

/*
 * This file manages two concepts:
 * 1)  A servlet_s, which manages a single raftlet and all connections which have dialed through to it.
 * 2)  A server_s, which binds to a port, listens for new connections and authenticates them with registered raftlets.
 */
#define MAX_CLIENTS 64


typedef enum servlet_state {
  S_INIT, S_RUN, S_STOP_REQUESTED, S_DEAD  
} servlet_state;

struct client {
  traft_conninfo_t  info;
  int               fd;
};
// Represents the server for a single raftlet
typedef struct traft_servlet_s {
  pthread_mutex_t     guard;
  servlet_state       state;
  pthread_cond_t      state_change;
  struct client       clients[MAX_CLIENTS];
  int                 num_clients;
  pthread_t           poll_thread;
  traft_raftletinfo_t identity;
  void                *raftlet;
  traft_server_ops    ops;
} traft_servlet_s;

// Adds the client or returns -1 if we're already full
static int servlet_add_client(traft_servlet_s *s, int fd, traft_conninfo_t conn) {
  pthread_mutex_lock(&s->guard);
  if (s->num_clients == MAX_CLIENTS) {
    pthread_mutex_unlock(&s->guard);
    return -1;
  }
  s->clients[s->num_clients] = (struct client){.info = conn, .fd = fd};
  s->num_clients++;
  pthread_mutex_unlock(&s->guard);
  return 0;
}

static int servlet_get_clientinfo(traft_servlet_s *s, int fd, traft_conninfo_t *info) {
  pthread_mutex_lock(&s->guard);
  for (int i = 0 ; i < s->num_clients ; i++) {
    if (s->clients[i].fd == fd) {
      *info = s->clients[i].info;
      pthread_mutex_unlock(&s->guard);
      return 0;
    }
  }
  // fd not found
  pthread_mutex_unlock(&s->guard);
  return -1;
}

static void servlet_kill_client(traft_servlet_s *s, int fd) {
  if (fd < 1) { return; }

  pthread_mutex_lock(&s->guard);
  // find fd_idx in the array
  int fd_idx = -1;
  for (int i = 0 ; i < s->num_clients ; i++) {
    if (s->clients[i].fd == fd) {
      fd_idx = i;
      break;
    }
  }
  // move top element to our last position and decrement count
  if (fd_idx >= 0) {
    int last_idx = s->num_clients - 1;
    s->clients[fd_idx] = s->clients[last_idx];
    s->clients[last_idx].fd = -1;
    s->num_clients--;
  }
  pthread_mutex_unlock(&s->guard);

  close(fd);
}

static void servlet_close(traft_servlet_s *s) {
  // copy to temp buffer with lock held
  pthread_mutex_lock(&s->guard);
  int fds[MAX_CLIENTS];
  int num_fds = s->num_clients;
  for (int i = 0 ; i < s->num_clients; i++) {
    fds[i] = s->clients[i].fd;
    s->clients[i].fd = -1;
  }
  s->num_clients = 0;

  // close all
  for (int i = 0 ; i < num_fds ; i++) {
    if (fds[i]) { close(fds[i]); }
  }
  s->state = S_DEAD;
  pthread_cond_broadcast(&s->state_change);
  pthread_mutex_unlock(&s->guard);
}

// Sets up the provided array of pollfds and returns number of fds to poll for
static int servlet_prepare_poll(traft_servlet_s *s, struct pollfd *fds) {
  memset(fds, 0, sizeof(int) * MAX_CLIENTS);
  pthread_mutex_lock(&s->guard);
  for (int i = 0 ; i < s->num_clients ; i++) {
    fds[i].fd = s->clients[i].fd;
  }
  int count = s->num_clients;
  pthread_mutex_unlock(&s->guard);
  return count;
}

static void *servlet_run(void *arg) {
  traft_servlet_s *servlet = (traft_servlet_s*)arg;

  traft_buff req_buff;
  traft_resp resp;
  int err = traft_buff_alloc(&req_buff, 1024 * 1024);

  struct pollfd pollfds[MAX_CLIENTS];
  int polltimeout_ms = 200;
  traft_conninfo_t info;
  
  while (1) {
    // poll
    int num_poll_fds = servlet_prepare_poll(servlet, pollfds);
    err = poll(pollfds, num_poll_fds, polltimeout_ms);
    if (err == -1) { 
      goto SERVLET_DIE;
    }
    for (int i = 0 ; i < num_poll_fds ; i++) {
      if (pollfds[i].revents & (POLLHUP | POLLERR)) {
        servlet_kill_client(servlet, pollfds[i].fd);
        continue;
      }
      if (pollfds[i].revents & POLLIN) {
        servlet_get_clientinfo(servlet, pollfds[i].fd, &info);
        // TODO timeout
        traft_buff_readreq(&req_buff, pollfds[i].fd);
        int err = servlet->ops.handle_request(&servlet->raftlet, &info, &req_buff, &resp);
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
  servlet_close(servlet);
}

static void servlet_add_conn(traft_servlet_s* server, int client_fd, traft_hello *hello) {
  traft_conninfo_t clientinfo;
  memcpy(&clientinfo.client_id, &hello->client_id, 32);
  if (traft_buff_decrypt_sessionkey(hello, server->identity.my_sk, clientinfo.session_key) != 0) {
    // TODO decrypt error, tell client they're not auth'd and hangup
  }
  // add to client_set
  servlet_add_client(server, client_fd, clientinfo);
}

static int servlet_stop(traft_servlet_s *server) {
  pthread_mutex_lock(&server->guard);
  while (server->state != S_DEAD) {
    pthread_cond_wait(&server->state_change, &server->guard);
  }
  pthread_mutex_unlock(&server->guard);
  return 0;
}

#define MAX_SERVLETS 4

typedef enum accepter_state {
  A_INIT, A_RUN, A_STOP_REQUESTED, A_DEAD  
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
    if (traft_buff_readhello(&hello, client_fd) == -1) {
      // kill conn
      close(client_fd);
      continue;
    }
    // locate raftlet for this cluster_id
    traft_servlet_s *found_servlet = NULL;
    
    for (int i = 0 ; i < server->num_raftlets ; i++) {
      traft_servlet_s *servlet = &server->servlets[i];
      traft_raftletinfo_t *raftlet_info = &servlet->identity;

      if (memcmp(&hello.cluster_id, raftlet_info->cluster_id, 16) == 0 
          && memcmp(&hello.server_id, raftlet_info->my_id, 32) == 0) {
        found_servlet = servlet;
        break;
      }
    }
    // No raftlet for this ID, kill..  should we send an error back to client?
    // TODO log
    if (found_servlet == NULL) { close(client_fd); continue; }
    // process raftlet
    servlet_add_conn(found_servlet, client_fd, &hello);
  }
  ACCEPTER_DIE:
  close(server->accept_fd);
  pthread_mutex_lock(&server->servlets_guard);
  pthread_cond_broadcast(&server->state_change);
  server->state = A_DEAD;
  pthread_mutex_unlock(&server->servlets_guard);
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
  int err = bind(server->accept_fd, (const struct sockaddr*)&in_any, sizeof(struct sockaddr_in));
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
  server->state = A_INIT;
  int err = bind_accepter_sock(server);
  if (err == -1) { goto START_SERVER_ERR; }
  memset(server->servlets, 0, sizeof(traft_servlet_s) * MAX_SERVLETS);
  err = pthread_create(&server->accept_thread, NULL, &traft_do_accept, server);
  if (err != 0) { goto START_SERVER_ERR; }
  // wait until serving
  wait_accepter_state(server, A_RUN);
  *ptr = server;
  return 0;

  START_SERVER_ERR:
  if (server->accept_fd > 0) { close(server->accept_fd); }
  free(server);
  return -1;
}


int traft_stop_server(traft_server server_ptr) {
  traft_accepter_s *server = (traft_accepter_s*) server_ptr;
  // TODO kill all servlets
  // mark to die
  pthread_mutex_lock(&server->servlets_guard);
  server->state = A_STOP_REQUESTED;
  // wait dead
  while (server->state != A_DEAD) {
    pthread_cond_wait(&server->state_change, &server->servlets_guard);
  }
  // clean up
  close(server->accept_fd);
  free(server);
}


int traft_srv_add_raftlet(traft_server server_ptr, traft_raftletinfo_t raftlet_info, void *raftlet) {
  traft_accepter_s *server = (traft_accepter_s*) server_ptr;
  pthread_mutex_lock(&server->servlets_guard);
  traft_servlet_s *servlet =  &server->servlets[server->num_raftlets];
  server->num_raftlets++;
  servlet->raftlet = raftlet;
  servlet->identity = raftlet_info;
  servlet->ops = server->ops;
  servlet->num_clients = 0;
  for (int i = 0 ; i < MAX_CLIENTS ; i++) {
    servlet->clients[i].fd = -1;
  }

  pthread_mutex_init(&servlet->guard, NULL);
  int err = pthread_create(&servlet->poll_thread, NULL, &servlet_run, servlet);
  if (err != 0) { 
    server->num_raftlets--; 
  }
  pthread_mutex_unlock(&server->servlets_guard);
  return err;
};

int traft_srv_stop_raftlet(traft_server server_ptr, traft_publickey_t raftlet_id) {
  traft_accepter_s *server = (traft_accepter_s*) server_ptr;
  pthread_mutex_lock(&server->servlets_guard);

  traft_servlet_s *found_servlet = NULL;
  int found_idx = 0;
  for (int i = 0 ; i < server->num_raftlets ; i++) {
    if (memcmp(server->servlets[i].identity.my_id, raftlet_id, sizeof(traft_publickey_t)) == 0) {
      found_servlet = &server->servlets[i];
      found_idx = i;
      break;
    }
  }
  pthread_mutex_unlock(&server->servlets_guard);
  if (found_servlet == NULL) {
    // No raftlets matched provided raftlet_id
    return -1;
  }

  traft_servlet_s *last_servlet =  &server->servlets[server->num_raftlets - 1];
  return servlet_stop(last_servlet);
};


