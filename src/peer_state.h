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


// Unique ID for a log entry
typedef struct traft_entry_id {
    uint64_t  term_id;
    uint32_t  idx;
} traft_entry_id;

typedef struct traft_peer_state {
	pthread_mutex_t guard;
  pthread_cond_t  on_change;
  traft_entry_id  local_committed_idx;
  traft_entry_id  quorum_idx;
} traft_peer_state;


#ifdef __cplusplus
}
#endif
