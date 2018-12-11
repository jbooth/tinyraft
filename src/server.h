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

#ifdef __cplusplus
extern "C" {
#endif

/** Server-side of a connected client */
typedef traft_conn struct {
  int       sock_fd;
  uint8_t   client_id[32];    // client's public key
  uint8_t   session_key[32];  // session key for encrypting new log entries
} traft_conn;

typedef traft_raftlet_private struct {
  traft_storage     storage;
  traft_pub_key     my_id;
  traft_private_key my_priv_key;
} traft_raftlet_private;

typedef traft_server_private struct {
  struct sockaddr_in  accept_addr;
  int                 accept_fd;
  pthread_t           accept_thread;
  pthread_mutex_t     raftlets_guard;
  raftlets            *raftlet_t[256];
  int                 num_raftlets;
} traft_server_private;

// Starts the provided server and assigns the provided pointer to it
int traft_start_server(uint16_t port, traft_server *ptr);

#ifdef __cplusplus
}
#endif


