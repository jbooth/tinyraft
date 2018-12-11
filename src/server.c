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


void * traft_serve(void *arg) {
  traft_server_private *server = (traft_server_private*) arg;
  
  // accept conns and delegate to raftlets
  struct sockaddr new_conn_addr;
  socklen_t new_conn_addrlen;
  while (1) {
    int client_fd = accept(server->accept_fd, &new_conn_addr, &new_conn_addrlen);
    // read hello
    // authenticate, confirm we have raftlet
    // write hello_resp
    // assign to raftlet 
  }
}

int traft_start_server(uint16_t port, traft_server *ptr) {
  // allocate server
  traft_server *server = malloc(sizeof(traft_server_private));
  memset(server, 0, sizeof(traft_server_private));
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

