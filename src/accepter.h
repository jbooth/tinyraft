#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sys/uio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <linux/in.h>

#include "buffers.h"
#include "wiretypes.h"
#include "raftlet.h"
#include "server.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Structure containing our accepter socket and all registered raftlets */
typedef struct traft_accepter_s {
  struct sockaddr_in  accept_addr;
  int                 accept_fd;
  pthread_t           accept_thread;
  pthread_mutex_t     servlets_guard;
  traft_raftlet_s     *raftlets[256];
  int                 num_raftlets;
} traft_accepter_s;

// Starts a server and assigns the provided pointer to it
int traft_start_server(uint16_t port, void **ptr);

// Stops the provided server and all attached raftlets.
int traft_stop_server(traft_accepter_s ptr);

// Adds the provided raftlet and starts relevant threads to serve it.
int traft_accepter_add_raftlet(traft_accepter_s *accepter, traft_raftlet_s *raftlet);

int traft_accepter_stop_raftlet(traft_accepter_s *accepter, traft_raftlet_s *raftlet);

#ifdef __cplusplus
}
#endif


