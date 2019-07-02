#pragma once

#include "tinyraft.h"
#include "buffers.h"
#include "raftlet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct traft_servlet {
    uuid_t            cluster_id;
    traft_secretkey_t my
} traft_servlet;

typedef struct traft_server_ops {
  int (*handle_request) (void *raftlet, traft_clientinfo *client, traft_buff *req, traft_resp *resp);

  int (*init_raftlet) (const char *storagepath, traft_statemachine_ops ops, void *state_machine, traft_raftlet_s *raftlet);

  int (*destroy_raftlet) (traft_raftlet_s *raftlet);
} traft_server_ops;

int traft_srv_start_server(uint16_t port, traft_server *ptr, traft_server_ops ops);

/** Requests shutdown of the provided acceptor and all attached raftlets. */
int traft_srv_stop_server(traft_server server_ptr);

/** 
  * Starts a raftlet serving the provided, initialized storagepath on the provided server.  
  * Allocates all resources necessary to process entries and starts threads before returning.
  */
int traft_srv_run_raftlet(const char *storagepath, traft_server server, traft_statemachine_ops ops, 
                            void *state_machine, traft_raftlet *raftlet);

/**
  * Requests that a raftlet stop running.  It will clean up all resources and terminate threads before returning.
  */
int traft_srv_stop_raftlet(traft_raftlet *raftlet);

#ifdef __cplusplus
}
#endif