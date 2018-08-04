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


int tinyraft_non_function(int i);

/** RPC client handle */
typedef struct rpc_client {
  uint32_t            next_reqno;      // incremented on send
  uint32_t            last_resp_reqno; // incremented on recv
  int sock_fd;
} rpc_client; 

/** Threadsafe RPC client handle */
typedef struct ts_rpc_client ts_rpc_client;

/** Future object representing a pending response */
typedef struct resp_future {
  ssize_t  written; // count of bytes sent in request, or negative if we had a write error
  uint32_t reqno;   // used by client to track which future gets which response
} resp_future;

// Request headers are all 64 bytes, responses are all 32 bytes.
#define RPC_REQ_LEN 64 
#define RPC_RESP_LEN 32

/** Shared request info, always the last 8 of the 64 request header bytes */
typedef struct req_info {
  uint32_t reqno;       // 4
  uint8_t  opcode;      // 5
  uint8_t  padding[3];  // 8
} req_info;

/** Generic request header type, cast to this to call functions below */
typedef struct generic_req {
  uint8_t   padding[56];  // 56
  req_info  info;         // 64
} generic_req;

/** Shared resp info, always the last 8 of the 32 response bytes */
typedef struct resp_info {
  uint32_t reqno;
  uint32_t status; 
} resp_info;

/** Generic response type, cast to this to call functions below */
typedef struct generic_resp {
  uint8_t     padding[24];
  resp_info   info;
} generic_resp;


// Client API Functions

/** Initialize client by connecting to provided sockaddr */
int init_client(rpc_client *client, struct sockaddr *addr, socklen_t addlen);

/** Send a request with no body and get a future response */
resp_future send_request(rpc_client *client, generic_req *req);

/** Waits until this response is ready and writes it to resp_bytes */
ssize_t await(rpc_client *client, resp_future *resp_handle, generic_resp *resp_bytes);

//int await(ts_rpc_client client, resp_future* resp_handle, uint8_t* resp_bytes);

// Server API Functions
typedef int (*server_handler)(uint8_t *req, uint8_t *resp);

/** 
 *  Bind the provided FD to the provided addr and serve clients indefinitely.
 *  Calls handler on each request.
 *  We take accept_fd as an argument to allow for tests to kill an executing thread by
 *  closing the fd.  It should have been created using socket().
 */
void serve_clients(int accept_fd, struct sockaddr *addr, socklen_t addrlen, server_handler handler);



/* Request definitions */
#define OPCODE_append_entries     1
#define OPCODE_replicate_entries  2
#define OPCODE_init_cluster       3
#define OPCODE_add_server         4
#define OPCODE_vote_req           5



/** Monotonically increasing per-client */
typedef struct client_idx {
  uint32_t  client_id;
  uint32_t  client_idx;
} client_idx; 

/** Request sent to the leader to add entries to the cluster */
typedef struct append_entries_hdr {
  uuid_t    leader_id;      // 16
  uint32_t  body_len;       // 20
  uint32_t  num_args;       // 24
  uint8_t   sys_msg;        // 25
  uint8_t   padding[31];    // 56
  req_info  info;           // 64
} append_entries_hdr;

/** Request sent by the leader to replicate entries to the cluster */
typedef struct replicate_entries_hdr {
  uuid_t    leader_id;      // 16
  uint64_t  leader_term;    // 24
  uint64_t  prev_log_term;  // 32
  uint32_t  prev_log_idx;   // 36
  uint32_t  ldr_commit_idx; // 40
  uint32_t  ldr_apply_idx;  // 44
  uint32_t  body_len;       // 50
  uint32_t  num_args;       // 54
  uint8_t   sys_msg;        // 55
  uint8_t   padding;        // 56
  req_info  info;           // 64
} replicate_entries_hdr;


typedef struct init_cluster_req {
  uuid_t      db_uniq_id;   // 16
  uuid_t      leader_id;    // 32
  uint64_t    curr_Term;    // 36
  uint8_t     padding[20];  // 56
  req_info    info;         // 64
} init_cluster_req;

typedef struct add_server_req {
  uuid_t      db_uniq_id;   // 16
  uuid_t      peer_id;      // 32
  uint8_t     padding[24];  // 56
  req_info    info;         // 64
} add_server_req;

typedef struct vote_req {
  uuid_t        candidate_id; // 16
  uuid_t        db_uniq_id;   // 32
  uint64_t      term;         // 40
  uint64_t      last_log_term;// 48
  uint32_t      last_log_idx; // 52
  uint32_t      padding;      // 56
  req_info      info;         // 64
} vote_req;


typedef struct append_entries_resp {
  
} append_entries_resp;

#ifdef __cplusplus
}
#endif
