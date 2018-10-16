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
#include "rpc.h"

// Log header, contains term-wide information.
typedef struct traft_log_header {
  uint64_t term;              // 8  Term ID
  uint32_t num_entries;       // 12 Number of entries we have space for in our mmap
  uint32_t max_committed;     // 16 Highest entry idx that's been committed locally
  uint32_t quorum_committed;  // 20 Highest entry idx that's been committed by a quorum
  uint8_t  magic[4];          // 24 Always the characters 'RAFT'
  uint8_t  padding[8];        // 32 
} traft_log_header;

// Metadata for an individual log entry, stored in the entries section of the log file.
// Indexed by this entry's IDX.  That is, term_log->entries[idx] will yield the metadata for that entry.
typedef struct traft_log_entry_md {
  uint64_t  entry_pos;    // 8, pos of entry in data section, points to beginning of AppendEntries header
  uint64_t  answer_pos;   // 16, pos of answer in answers file
  uint32_t  entry_len;    // 20 length of entry section including 64-byte AppendEntries header
  uint32_t  answer_len;   // 24 length of answer section
  uint8_t   padding[8];   // 32
} traft_log_entry_md;

typedef struct log_state {
  traft_entry_id last_committed;
  traft_entry_id quorum_committed;
} log_state;

/**
 * Represents a single term using 2 files (entries and answers).
 * The entries file contains 3 sections:
 *    A header indicating how many entries are in the file (mmapped)
 *    An index in the form of a contiguous array of struct traft_log_entry_md
 *    Data section containing actual entries in line with their AppendEntriesRequest headers.
 * The answers file contains output of executed commands.
 */
typedef struct traft_log_term_log {
  pthread_mutex_t entries_lock;
  pthread_cond_t entries_changed;
  traft_log_header *header;
  traft_log_entry_md *entries; 
  size_t map_len; // mmap is shared between header and entries; starts at header and is map_len long
  int entries_fd; 
  int answers_fd;
} traft_log_term_log;

#define LOGS_RETAINED 10
typedef struct log_set {
  traft_log_term_log logs[LOGS_RETAINED];
  pthread_rwlock_t membership_lock;
} log_set;

log_state ls_get_log_state(log_set *logs);

/**
 * Represents a contiguous chunk of entries in a termfile.
 * first_entry and last_entry may be identical.
 * 
 * On first usage, initialize with 0s
 */
typedef struct send_queue {
  traft_entry_id last_entry;
  int fd;
  off_t pos;
  size_t count;
} send_queue;

int traft_log_open(traft_log_term_log *log, const char *basedir, uint64_t term_id);

int traft_log_create(traft_log_term_log *log, const char *basedir, uint64_t term_id, uint32_t num_entries);

/**
 * Writes the given entry with header included.
 * Req_info portion of append_entries_request can/should be zeroed, we don't look at it.
 */
int write_entry(log_set *logs, generic_req *append_entries_request, int socket);

void set_quorum_cmt(log_set *logs, traft_entry_id quorum_committed);

/**
 * If there are subsequent entries to current_queue.last_entry, then 
 * populate send_queue with information for future sendfile calls.
 * 
 * Used by replicator to forward entries.
 * 
 * Returns the number of logs added, or -1 on error.
 */ 
int wait_more(log_set* logs, send_queue *current_queue, int max_entries, int max_wait_ms);

/**
 * Uses sendfile to send a batch of entries.  Updates current_queue's state appropriately.
 */
ssize_t send_entries(int follower_fd, log_set* logs, send_queue *current_queue);

/** Gets the max log ID we've recognized quorum for. */
traft_entry_id get_quorum_id(log_set *logs);

/** Sets the provided ID as quorum iff it's after the current quorum.  Does not backtrack.  */
void set_quorum_id(log_set *logs, traft_entry_id quorum_id);
