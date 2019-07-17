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

typedef struct raftlet_state {
    pthread_mutex_t     guard;
    // constant state
    char                *storage_path; // heap-allocated
    // volatile state, some of this is persisted in relevant termlog
    traft_entry_id      max_committed_local;
    traft_entry_id      quorum_committed;
    traft_entry_id      max_applied_local;
    uuid_t              cluster_id;
    uint64_t            last_snapshot_term_id;
    traft_publickey_t   leader_id;
    // persistent state
    traft_publickey_t   last_voted_for;
    uint64_t            current_term_id;
} raftlet_state;

#ifdef __cplusplus
}
#endif
