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
#include "storage.h"
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
