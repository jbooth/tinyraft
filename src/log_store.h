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


// Log header, contains term-wide information.
typedef struct entries_header {
  uint64_t term;              // 8
  uint32_t max_entries;       // 12
  uint32_t max_idx;           // 16
  uint8_t  padding[16];       // 32 
} entries_header;

// Metadata for an individual log entry, stored in the entries section of the log file.
typedef struct log_entry {
  uint64_t  entry_pos;    // 8, pos of entry in data section, points to beginning of AppendEntries header
  uint64_t  answer_pos;   // 16, pos of answer in answers file
  uint32_t  entry_idx;    // 20 index of this entry within the file's term
  uint32_t  entry_len;    // 24 length of entry section including 64-byte AppendEntries header
  uint32_t  answer_len;   // 28 length of answer section
  uint8_t   majority_committed; // 29, boolean
  uint8_t   applied_local; // 30, boolean
  uint8_t   padding[2];   // 32
} log_entry;

/**
 * Represents 3 files (entries index, entries data and answers) for a single term.
 * The entries index is an mmapped array of struct log_entrys with indexes into the 
 * entries_data file and the answers file.
 * The entries file is raw requests with their ReplicateEntries header in line.
 * The answers file contains output of executed commands.
 */
typedef struct term_log {
  // guarded by entries_lock
  pthread_mutex_t entries_lock;
  pthread_cond_t entries_changed;
  entries_header* header;
  log_entry* entries; 
  int index_fd;

  // data is unguarded -- only one thread writes to entries or answers
  int entries_fd; 
  int answers_fd;
} term_log;

#define LOGS_RETAINED 10
typedef struct log_set {
  term_log logs[LOGS_RETAINED];
  pthread_rwlock_t membership_lock;
} log_set;