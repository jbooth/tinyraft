#include <stddef.h>
#include <sys/uio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>

#include "tinyraft.h"
#include "buffers.h"
#include "wiretypes.h"
#include "raftlet.h"

#define MAX_CLIENTS 32

// track fds so we can make sure all are closed
typedef struct traft_client_set {
  pthread_spinlock_t  guard;
  int                 fds[MAX_CLIENTS];
  traft_clientinfo    info[MAX_CLIENTS];
  int                 count;
} traft_client_set;

// Represents the server for a single raftlet
typedef struct traft_servlet_s {
  traft_client_set  clients;
  traft_raftlet_s   raftlet;
  int (*handle_request) (traft_raftlet_s *raftlet, traft_req *req, int client_fd);
} raftserver;

// Method to accept a new connection
void traft_add_conn(traft_raftlet_s* raftlet, int client_fd, traft_hello *hello);

// Server thread method, handles all connected clients
void * traft_serve_raftlet(void *arg);
