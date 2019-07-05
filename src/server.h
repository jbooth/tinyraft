#pragma once

#include "tinyraft.h"
#include "buffers.h"
#include "raftlet.h"

#ifdef __cplusplus
extern "C" {
#endif


#define TRAFT_SERVER_HANDLE_SUCCESS 0
#define TRAFT_SERVER_KILL_CLIENT  1
#define TRAFT_SERVER_KILL_RAFTLET 2

typedef struct traft_server_ops {
  int (*handle_request) (void *raftlet, traft_clientinfo_t *client, traft_buff *req, traft_resp *resp);


  void (*destroy_raftlet) (void *raftlet);
} traft_server_ops;

int traft_srv_start_server(uint16_t port, traft_server *server_ptr, traft_server_ops ops);

/** Requests shutdown of the provided acceptor and all attached raftlets. */
int traft_srv_stop_server(traft_server server);

/**
 * Adds a raftlet and starts
 */
int traft_srv_add_raftlet(traft_server server, traft_raftletinfo_t raftlet_info, void *raftlet);

/**
  * Requests that a raftlet stop running.  It will clean up all resources and terminate threads before returning.
  */
int traft_srv_stop_raftlet(traft_server server, traft_publickey_t raftlet_id);

#ifdef __cplusplus
}
#endif