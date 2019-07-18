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
#include "raftlet_state.h"

typedef struct raftlet_state {

} raftlet_state;


typedef struct traft_raftlet_s {    
    traft_rwlock_t     guard;
    // constant state
    char                *storage_path; // heap-allocated

    // volatile state, some of this is persisted in the relevant termlog
    traft_raftletinfo_t   info;
    traft_entry_id        max_committed_local;
    traft_entry_id        quorum_committed;
    traft_entry_id        max_applied_local;
    uuid_t                cluster_id;
    uint64_t              last_snapshot_term_id;
    traft_publickey_t     leader_id;
    traft_symmetrickey_t  current_termkey;
    traft_termlog         current_termlog;

    // persistent state, persisted in storage_path/STATE
    traft_publickey_t   last_voted_for;
    uint64_t            current_term_id;
} traft_raftlet_s;

int traft_raftlet_handle_req(traft_raftlet_s *raftlet, traft_conninfo_t *client, traft_buff *req, traft_resp *resp);


#ifdef __cplusplus
}
#endif
