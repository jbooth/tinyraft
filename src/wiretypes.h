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
  uint32_t  this_idx        // 44
  uint32_t  prev_idx        // 48
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

typedef struct append_entries_resp {
  uint64_t appended_term;    // 8
  uint64_t quorum_term;      // 16
  uint32_t appended_idx;     // 20
  uint32_t quorum_idx;       // 24
  uint8_t  padding[8];       // 32
} append_entries_resp;

#ifdef __cplusplus
}
#endif
