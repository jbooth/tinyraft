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

#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "tinyraft.h"
#include "log_store.h"
#include "rpc.h"

/*
 * Logs are stored in files named after the hex of the term_id (16 chars).
 * We have 2 files, the entries file and the answers file.  Entries contains
 * input to the state machine while answers contains output.
 * 
 * Contents of entries file have 3 sections:
 *  log_header    global info, mmapped as struct
 *  entry_md*     entry metadata table, mmappped and indexed by entry_idx
 *  log_content   contents of AppendEntries requests on their way through the system
 * 
 * We also have an answers file,  attached to term_log and pointed at by each entry's 
 * answer_pos, answer_len.
 */

// Each log_part has 3 files:
//  1) index of all entries, mmapped (entries_fd)
//  2) file of actual log contents (content_fd)
//  3) file of answers produced by state machine (answers_fd)
typedef struct term_log {
  // guarded by entries_lock
  pthread_mutex_t entries_lock;
  pthread_cond_t entries_changed;
  entries_header* header;
  log_entry* entries; 

  // content_wl guards access to data section of file
  pthread_mutex_t content_wl;
  int entries_fd; 
  int answers_fd;
} term_log;

static int get_entries(term_log* lpart, uint32_t last_entry_idx, log_entry* destination, int count) {
  // block until max_entry is something we haven't seen yet
  pthread_mutex_lock(&lpart->entries_lock);
  log_entry* max_entry = &lpart->entries[lpart->header->last_written_idx];
  while (max_entry->entry_idx > last_entry_idx) {
    pthread_cond_wait(&lpart->entries_changed, &lpart->entries_lock);
    max_entry = &lpart->entries[lpart->header->last_written_idx];
  }
  pthread_mutex_unlock(&lpart->entries_lock);

  // copy
  int entries_offset = last_entry_idx + 1;
  int copied = 0;
  while (count && entries_offset <= max_entry->entry_idx) {
    destination[count] = lpart->entries[entries_offset];
    entries_offset++;
    copied++;
    count--;
  }
  return copied;
}

static int close_log(term_log* lgpart) {
  munmap(lgpart->entries, lgpart->header->max_entries * sizeof(log_entry));
  close(lgpart->entries_fd);
  close(lgpart->answers_fd);
  return 0;
}

static int build_path(char* dest, char* basedir, uint64_t term_id, char* extension) {
  size_t available = strnlen(dest, 4096);
  size_t basedir_len = strnlen(basedir, available);
  //size_t 
  return 0;
}

static int open_term_log(term_log* lgpart, char* basedir) {
  // create files
  char filepath[1024];
  size_t basedir_len = strnlen(basedir, 1024);
  if (basedir_len > 1000) {
    return -1;
  }
  strncpy(filepath, basedir, basedir_len);
  //sprintf(&filepath, 
  // fallocate

  // mmap
  return 0;
}

// We manage 2 log_parts:  current and previous
// When current fills up, we delete previous and create a new one
typedef struct log_store {
  term_log*    previous;
  term_log*    current;
} log_store;
