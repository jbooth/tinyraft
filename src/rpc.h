/*
   Copyright 2018 Google LLC

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

	 Author Jay Booth
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include <uuid/uuid.h>
#include <sys/socket.h>
#include "wiretypes.h"


/* Request definitions */
#define OPCODE_append_entries     1
#define OPCODE_replicate_entries  2
#define OPCODE_init_cluster       3
#define OPCODE_add_server         4
#define OPCODE_vote_req           5

/** Shared request info, always the last 8 of the 64 request header bytes */
typedef struct req_info {
  uint32_t reqno;       // 4
  uint8_t  opcode;      // 5
  uint8_t  sys_msg;     // 6
  uint8_t  padding[2];  // 8
} req_info;

/** Generic request header type, cast to this to call functions below */
typedef struct generic_req {
  union {
    forward_entries_req forward_entries;
    append_entries_req append_entries;
    init_cluster_req init_cluster;
    add_server_req add_server;
    would_vote_req would_vote;
    request_vote_req request_vote;
  } message;
  req_info  info;         // 64
} generic_req;


/** Shared resp info, always the last 8 of the 32 response bytes */
typedef struct resp_info {
  uint32_t reqno;
  uint32_t status; 
} resp_info;

/** Generic response type, cast to this to call functions below */
typedef struct generic_resp {
  union {
    append_entries_resp append_entries;
  } message;
  resp_info   info;
} generic_resp;

int tinyraft_non_function(int i);

typedef struct rpc_conn {
  uuid_t   remote_peer_id;
  uint32_t session_id;
  int      fd;
} rpc_conn;

/** RPC client handle */
typedef struct rpc_client {
  uint32_t            client_id;           // random ID, server may reject on collision with other client
  uint32_t            next_reqno;      // incremented on send
  uint32_t            last_resp_reqno; // incremented on recv
  int sock_fd;
} rpc_client; 

/** Threadsafe RPC client handle */
typedef struct ts_rpc_client {
  rpc_client          client;
  pthread_mutex_t     write_lock;
  pthread_mutex_t     read_lock;
  pthread_cond_t      last_resp_changed;
} ts_rpc_client;

/** Future object representing a pending response */
typedef struct resp_future {
  ssize_t  written; // count of bytes sent in request, or negative if we had a write error
  uint32_t reqno;   // used by client to track which future gets which response
} resp_future;

// Client API Functions

typedef struct hello_msg {
  uuid_t cluster_id;

} hello_request;

/** Initialize client by connecting to provided sockaddr */
int init_client(rpc_client *client, struct sockaddr *addr, socklen_t addlen);

/** */
int send_req_raw(int client_fd, generic_req *req);

/** Send a request with no body and get a future response */
resp_future send_request(rpc_client *client, generic_req *req);

/** Waits until this response is ready and writes it to resp_bytes */
ssize_t await(rpc_client *client, resp_future *resp_handle, generic_resp *resp_bytes);

/** Blocks reading 32 bytes into the provided resp */
int read_resp_raw(int client_fd, generic_resp *resp_bytes);

//int await(ts_rpc_client client, resp_future* resp_handle, uint8_t* resp_bytes);

/* 
 * We want to support multiple servers sharing a port, .  
 */
typedef int (*server_handler)(uint8_t *req, uint8_t *resp);

typedef struct server_state {
  int accept_fd;
  int epoll_fd;
  struct sockaddr* addr;
  socklen_t addrlen;
} server_state;
/** 
 *  Bind the provided FD to the provided addr and serve clients indefinitely.
 *  Calls handler on each request.
 *  We take accept_fd as an argument to allow for tests to kill an executing thread by
 *  closing the fd.  It should have been created using socket().
 */
void serve_clients(int accept_fd, struct sockaddr *addr, socklen_t addrlen, server_handler handler);

#ifdef __cplusplus
}
#endif
