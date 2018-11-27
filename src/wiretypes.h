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

// Request types 
#define TRAFT_REQTYPE_NEWENTRY    1
#define TRAFT_REQTYPE_APPENDENTRY 2
#define TRAFT_REQTYPE_REQVOTE     3

/** Common to all requests, always the last 24 bytes */
typedef struct traft_reqinfo {
  uint32_t  body_len;     // 4
  uint8_t   req_type;     // 5
  uint8_t   padding[3];   // 8
  uint8_t   auth_tag[16]; // 24
} traft_reqinfo;

/** Generic request with type and length */
typedef struct traft_req {
  uint8_t       padding[40];  // 40 type-specific data
  traft_reqinfo info;         // 64
} traft_req;
#define RPC_REQ_LEN 64 

/** Request sent to the leader to add entries to the cluster */
typedef struct traft_newentry_req {
  uint64_t  term_id;        // 8  Term we're trying to append to, identifies our key
  uint32_t  client_idx;     // 12 Monotonically increasing per-client value, resets on new term
  uint32_t  body_len;       // 16 
  uint16_t  client_id;      // 18 Client short ID
  uint8_t   padding[29];    // 47
  uint8_t   message_type;   // forward_entries
  uint8_t   auth_tag[16];   // 64 Poly1305 MAC
} traft_newentry_req;

// Length, from front of struct, of section used as 'additional data' for MAC
#define forward_entries_AD_len 48

/** 
  * Request sent by the leader to replicate entries to the cluster.
  * Contains term and index for this entry, prev entry, and quorum entry.
  * If the values for 'this' are all 0, this is a heartbeat request.
  */
typedef struct traft_appendentry_req {
  uint64_t  this_term;      // 8 , term of this entry
  uint64_t  prev_term;      // 16, term of prev entry
  uint64_t  quorum_term;    // 24, term of max quorum commit
  uint32_t  this_idx;       // 28, termlog idx of this entry
  uint32_t  prev_idx;       // 32, termlog idx of prev entry
  uint32_t  quorum_idx;     // 36, termlog idx of max quorum commit
  uint8_t   padding[4];     // 40
  uint32_t  body_len;       // 40, length of body behind this header
  uint8_t   message_type;   // 41, append_entries
  uint8_t   auth_tag[16];   // 64, Poly1305 MAC
} traft_appendentry_req;

// AE message types
#define TRAFT_AE_NORMAL     0
#define TRAFT_AE_HEARTBEAT  1
#define TRAFT_AE_TERMCHANGE 2
// Length, from front of struct, of section used as 'additional data' for MAC
#define append_entries_AD_len 48


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

#define RPC_RESP_LEN 32

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
