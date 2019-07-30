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
#include "tinyraft.h"
#include "wiretypes.h"
#include "storage.h"

#define TRAFT_MAX_PIPELINE_REQ 32
typedef struct traft_client_s {
    int                 fd;
    traft_conninfo_t    info;
    uint32_t            pending_reqs[TRAFT_MAX_PIPELINE_REQ];
    uint8_t             *answer_buffs[TRAFT_MAX_PIPELINE_REQ];
    uint32_t            num_pending;
    uint32_t            next_reqno;
} traft_client_s;


typedef enum traft_raftlet_mode{
    TRAFT_RAFTLET_MODE_LEADER,
    TRAFT_RAFTLET_MODE_CANDIDATE,
    TRAFT_RAFTLET_MODE_FOLLOWER
} traft_raftlet_mode;

typedef struct traft_raftlet_s {

    // constant state
    char                  *storage_path; // heap-allocated, null-terminated
    traft_raftletinfo_t   info;

    // volatile state
    pthread_mutex_t       guard;
    pthread_cond_t        changed;
    traft_server_config   current_config;
    traft_entry_id        max_committed_local;
    traft_entry_id        quorum_committed;
    traft_entry_id        max_applied_local;
    uuid_t                cluster_id;
    traft_publickey_t     leader_id;
    traft_symmetrickey_t  current_termkey;
    traft_termlog         current_termlog;
    traft_raftlet_mode    mode;

    traft_client_s        client_state;


    // persistent state, persisted in storage_path/STATE
    traft_publickey_t   last_voted_for;
    uint64_t            current_term_id;
} traft_raftlet_s;

int traft_raftlet_handle_req(traft_raftlet_s *raftlet, traft_conninfo_t *client, traft_buff *req, traft_resp *resp);

/** Writes a request to the leader, and stores the request_id in the provided location.  */
int traft_raftlet_send(traft_buff *newentry_request, uint32_t *request_id);

/** Awaits a pending request, writing the response, if any, to response_buff */
int traft_raftlet_await(uint32_t request_id, uint8_t *response_buff, size_t max_response_len);

/** 
 * Blocks until the log has surpassed the provided values.  
 * Writes new values to provided locations before returning.
 */
int traft_raftlet_waitnext(traft_raftlet *raftlet, 
                            traft_entry_id *quorum_commit, 
                            traft_entry_id *local_commit,
                            int max_wait_ms);



/** Used to spin off another thread which will, if we are leader, replicate entries to all followers. */
void* traft_replicate(void *raftlet);

/** Used to spin off a thread which will read responses for pending requests to leader and notify waiting clients. */
void* traft_runclient(void *raftlet);

/** Used to spin off a thread which will, if we're in candidate state, campaign for leadership */
void* traft_campaign(void *raftlet);

/** Used to spin off a thread which will apply committed entries to the state machine. */
void* traft_apply(void *raftlet);

#ifdef __cplusplus
}
#endif
