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

#include <pthread.h>
#include <stdint.h>
#include "wiretypes.h"
#include "termlog.h"
#include "buffers.h"
#include "rwlock.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct traft_peerstate {
  uint64_t  committed_term;
  uint64_t  quorum_term;
  uint32_t  committed_idx;
  uint32_t  quorum_idx;
} traft_peerstate;

typedef struct traft_storage {
  pthread_mutex_t peerstate_lock;
  pthread_cond_t  peerstate_changed;
  traft_peerstate peerstate;

  // Guards changeover between terms.  Normal operation acquires readlock, term change acquires writelock.
  traft_rwlock_t termlock;
  traft_termlog current_term;
  traft_termlog prev_term;
  char          storage_path[4096]; 
} traft_storage;

int traft_storage_open(traft_storage *storage, const char *path);

int traft_storage_close(traft_storage *storage);


/** Replicate a new entry from the leader, changing terms if necessary. */
int traft_storage_write_entry(traft_storage *storage, traft_appendentry_req *header, int client_fd, traft_buff *work_buff);

/** Create a new entry as the leader in the current term. */
int traft_storage_new_entry(traft_storage *storage, traft_newentry_req *header, int client_fd, traft_buff *work_buff);

/** Starts a new term with the new provided config. */
int traft_config_change(traft_storage *storage);

/** If new_quorum is > our current quorum, update current quorum and persist to disk. */
int traft_storage_update_quorum(traft_storage *storage, traft_entry_id new_quorum);

/** Blocks until we've added more entries locally or until max_wait_ms has elapsed. */
int traft_storage_wait_more_local(traft_storage *storage, traft_entry_id prev_max_entry, 
                                  traft_entry_id *new_max_entry, int max_wait_ms);

/** Blocks until we've reached quorum for more entries, or until max_wait_ms has elapsed */
int traft_storage_wait_more_quorum(traft_storage *storage, traft_entry_id prev_quorum, 
                                   traft_entry_id *new_quorum, int max_wait_ms);
int traft_storage_send_entries(traft_storage *storage, int follower_fd, 
                               traft_entry_id first_entry, traft_entry_id last_entry);


int traft_storage_apply_entries(traft_termlog *log, traft_entry_id first_entry, traft_entry_id last_entry,
                                  void *state_machine, traft_statemachine_ops ops);

#ifdef __cplusplus
}
#endif

