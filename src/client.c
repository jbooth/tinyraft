#include <pthread.h>
#include "wiretypes.h"
#include "rwlock.h"


#define MAX_PIPELINED_WRITES 32

typedef struct traft_fwdentries_client_t {
    traft_rwlock_t guard; 
    uint32_t client_idx_max_ack;
    traft_connection_t conn;
    pthread_t response_reader;
} traft_fwdentries_client_t;