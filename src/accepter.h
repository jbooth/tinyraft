#pragma once

#include <stdint.h>
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
#include "raftlet.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Structure containing our accepter socket and all registered raftlets */
typedef traft_accepter struct {
  struct sockaddr_in  accept_addr;
  int                 accept_fd;
  pthread_t           accept_thread;
  pthread_mutex_t     raftlets_guard;
  raftlets            *traft_raftlet_s[256];
  int                 num_raftlets;
} traft_accepter;

// Starts a server and assigns the provided pointer to it
int traft_start_server(uint16_t port, traft_server *ptr);

// Stops the provided server and all attached raftlets.
int traft_stop_server(traft_server ptr);

// Adds the provided raftlet and starts relevant threads to serve it.
int traft_add_raftlet(traft_accepter *accepter, traft_raftlet_s *raftlet);

int traft_stop_raftlet(traft_accepter *accepter, traft_raftlet_s *raftlet);



#ifdef __cplusplus
}
#endif


