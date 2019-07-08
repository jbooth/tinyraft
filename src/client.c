#include <pthread.h>
#include "wiretypes.h"
#include "rwlock.h"

typedef struct traft_pending_write_internal {
    traft_pending_write     write_id;
    traft_entry_id          assigned_entry;
} traft_pending_write_internal;

#define MAX_PIPELINED_WRITES 32

typedef struct traft_fwdentries_client_t {
    traft_rwlock_t guard; 
    traft_pending_write_internal pending_writes[MAX_PIPELINED_WRITES];
    uint32_t 
    uint32_t next_fwdentries_clientidx;
    traft_connection_t conn;
    pthread_t response_reader;

} traft_fwdentries_client_t;