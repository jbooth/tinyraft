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
#include "tinyraft.h"
#include "wiretypes.h"
#include "buffers.h"
#include "rwlock.h"

#ifdef __cplusplus
extern "C" {
#endif
// Log header, contains term-wide information.
typedef struct traft_log_header {
  uint64_t term;                  // 8  Term ID
  uint32_t max_entries;           // 12 Number of entries we have space for in our mmap
  uint32_t local_committed_idx;   // 16 Highest entry idx that's been committed locally
  uint32_t quorum_committed_idx;  // 20 Highest entry idx that's been committed by a quorum
  uint8_t  magic[4];              // 32 Always the characters 'RAFT'
} traft_log_header;

// Metadata for an individual log entry, stored in the entries section of the log file.
// Indexed by this entry's IDX.  That is, term_log->entries[idx] will yield the metadata for that entry.
typedef struct traft_log_entry_md {
  uint32_t  entry_pos;  // Start of append_entries header in entries file for this idx
  uint32_t  entry_len;  // Length of entry including header
} traft_log_entry_md;

/**
 * Represents a single term using a single file for entries.
 * The entries file contains 3 sections:
 *    A header indicating how many entries are in the file (mmapped)
 *    An index in the form of a contiguous array of struct traft_log_entry_md
 *    Data section containing actual entries in line with their AppendEntriesRequest headers.
 */
typedef struct traft_termlog {
  traft_rwlock_t        lock;
  traft_symmetrickey_t  termkey;
  traft_log_header      *header;
  traft_log_entry_md    *entries;
  size_t map_len; // mmap is shared between header and entries; starts at header and is map_len long
  int entries_fd;
} traft_termlog;


/**
 *
 */
int traft_termlog_leader_create(traft_termlog *log, const char *basedir, uint64_t term_id, uint32_t num_entries);

int traft_termlog_follower_create(traft_termlog *log, const char *basedir, traft_buff *new_cfg);

int traft_termlog_open(traft_termlog *log, const char *basedir, uint64_t term_id);
/**
 * Writes the provided 
 */
int traft_termlog_append_entry(traft_termlog *log, traft_buff *work_buff);

/**
 * Populates *newest_idx with the newest_idx in this log,
 * waiting up to max_wait_ms for a newer entry than last_idx.
 *
 * Used by replicator to forward entries.
 *
 * Returns 0 on success, -1 on error.
 */
int traft_termlog_wait_more(traft_termlog *log, uint32_t last_idx, uint32_t *newest_idx, int max_wait_ms);

/**
 * Uses sendfile to send a batch of entries.  Updates current_queue's state appropriately.
 */
int traft_termlog_send_entries(traft_termlog *log, int follower_fd, int32_t first_idx, int32_t last_idx);

/**
 * Applies the provided entry IDs to the provided state machine.
 */
int traft_termlog_apply_entries(traft_termlog *log, int32_t first_idx, int32_t last_idx, void *state_machine, traft_statemachine_ops ops);

#ifdef __cplusplus
}
#endif