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

// Request headers are all 64 bytes, responses are all 32 bytes.
#define RPC_REQ_LEN 64 
#define RPC_RESP_LEN 32

/** Request sent to the leader to add entries to the cluster */
typedef struct forward_entries_req {
  uuid_t    leader_id;      // 16
  uint32_t  body_len;       // 20
  uint32_t  num_args;       // 24
  uint8_t   padding[32];    // 56
} forward_entries_req;

/** Request sent by the leader to replicate entries to the cluster.
    Contains term and index for this entry, prev entry, and quorum entry.
    If the values for 'this' are all 0, this is a heartbeat request.
   */
typedef struct append_entries_req {
  uuid_t    leader_id;      // 16
  uint64_t  this_term;      // 24
  uint64_t  prev_term;      // 32
  uint64_t  quorum_term;    // 40
  uint32_t  this_idx;       // 44
  uint32_t  prev_idx;       // 48
  uint32_t  quorum_idx;     // 52
  uint32_t  body_len;       // 56
} append_entries_req;


typedef struct init_cluster_req {
  uuid_t      db_uniq_id;   // 16
  uuid_t      leader_id;    // 32
  uint64_t    curr_Term;    // 36
  uint8_t     padding[20];  // 56
} init_cluster_req;

typedef struct add_server_req {
  uuid_t      db_uniq_id;   // 16
  uuid_t      peer_id;      // 32
  uint8_t     padding[24];  // 56
} add_server_req;

typedef struct request_vote_req {
  uuid_t        candidate_id; // 16
  uuid_t        db_uniq_id;   // 32
  uint64_t      term;         // 40
  uint64_t      last_log_term;// 48
  uint32_t      last_log_idx; // 52
  uint32_t      padding;      // 56
} request_vote_req;


typedef struct would_vote_req {
  uuid_t        candidate_id; // 16
  uuid_t        db_uniq_id;   // 32
  uint64_t      term;         // 40
  uint64_t      last_log_term;// 48
  uint32_t      last_log_idx; // 52
  uint32_t      padding;      // 56
} would_vote_req;

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

typedef struct append_entries_resp {
  uint64_t committed_term;    // 8     // Last term this follower's committed
  uint64_t quorum_term;       // 16		 // This follower's opinion on what quorum term is
  uint32_t committed_idx;     // 20		 // Last index this follower's committed
  uint32_t quorum_idx;        // 24		 // This follower's opinion on quorum idx
  uint8_t  padding[8];        // 32
} append_entries_resp;

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


#ifdef __cplusplus
}
#endif
