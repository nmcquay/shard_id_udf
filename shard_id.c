/*

UDF for MySQL:
next_shard_id(<shard_id>) -- generates an id for a given shard (MAX of 1024)
shard_id_to_ms(<id>) -- converts a generated id to ms since epoch 1970-01-01

From shell (assuming in directory with Makefile and source):
% make
% sudo cp shard_id.so /usr/lib/mysql/plugin/
% mysql
mysql> create function next_shard_id returns integer soname 'shard_id.so';
mysql> create function shard_id_to_ms returns integer soname 'shard_id.so';

-------------------------------------------------------------------------------
Implementation based on Instagram sharding ids described at:
http://instagram-engineering.tumblr.com/post/10853187575/sharding-ids-at-instagram
logical shard id and auto-increment bit sizes were flipped in this
implementation also the timestamp portion is different with a 31-bit second
based timestamp and a 10-bit based millisecond-like number

This id generation scheme should be able to provide up to:
8192 unique ids per shard per millisecond-like time interval
until the morning of June 19, 2081

*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
#include <mysql.h>

#define INTERNAL_EPOCH 1370070000        // 2013-06-01 00:00:00
#define INTERNAL_EPOCH_MS 1370070000000  // 2013-06-01 00:00:00.000
#define MAX_SHARDS 1024                  // 10-bits for unique shards
#define MAX_SHARD_VALUE 0x3ff            // 10-bits for unique shards
#define MAX_COUNTER_VALUE 0x1fff         // 13-bits for a counter value

static uint16_t ids[MAX_SHARDS] = {[0 ... MAX_SHARDS - 1] = 0};
static pthread_mutex_t mtx[MAX_SHARDS] = {[0 ... MAX_SHARDS - 1] = PTHREAD_MUTEX_INITIALIZER};

// @return 51-bit integer:
//  31-bit integer of seconds since epoch and 20-bits of microseconds
uint64_t get_usec_epoch() {
    struct timeval tv;
    uint64_t usecs;
    gettimeofday(&tv, NULL);
    usecs = (tv.tv_sec - INTERNAL_EPOCH) << 20;
    return usecs | (tv.tv_usec & 0xfffff); //make sure tv_usec uses 20-bits
}

// @return 51-bit integer:
//  51-bits of microseconds since internal epoch
uint64_t get_real_usec_epoch() {
    struct timeval tv;
    uint64_t usecs;
    gettimeofday(&tv, NULL);

    //assume max tv_usec value of 999999
    usecs = (tv.tv_sec - INTERNAL_EPOCH) * 1000000;
    return usecs + tv.tv_usec;
}

// @return 41-bit integer:
//  31-bit integer of seconds since epoch and 10-bits of millisecond-like
uint64_t get_msec_epoch() {
    return get_usec_epoch() >> 10;
}

// @return 41-bit integer:
//  41-bits of milliseconds since internal epoch
uint64_t get_real_msec_epoch() {
    return get_real_usec_epoch() / 1000;
}

// validates the mysql inputs from next_shard_id
my_bool next_shard_id_init(UDF_INIT * const initid, UDF_ARGS * args, char * const msg) {
    if (args->arg_count != 1) {
        snprintf(msg, MYSQL_ERRMSG_SIZE, "Usage: next_shard_id(<shard>)");
        return 1;
    }
    if( *((uint64_t *)args->args[0]) >= MAX_SHARDS ) {
        snprintf(msg, MYSQL_ERRMSG_SIZE, "shard cannot be %d or greater", MAX_SHARDS);
        return 1;
    }

    args->arg_type[0] = INT_RESULT;
    initid->maybe_null = 0;
    initid->const_item = 0;
    initid->ptr = NULL;
    return 0;
}

// called by mysql when next_shard_id(<shard>) is evaluated
// @return 64-bit integer:
//  31-bit epoch + 10-bit milliseconds + 13-bit counter + 10-bit shard id
uint64_t next_shard_id(
        UDF_INIT *initid __attribute__((unused)),
        UDF_ARGS *args,
        char *is_null __attribute__((unused)),
        char *error) {
    uint64_t shard = *((uint64_t *)args->args[0]);
    uint64_t now = get_msec_epoch() << 23;
    uint32_t lower_bits;

    if (pthread_mutex_lock(&mtx[shard]) != 0) {
        *error = 1;
        return 0;
    }
    lower_bits = (ids[shard] << 10) | shard;
    ids[shard] = (ids[shard] + 1) & MAX_COUNTER_VALUE;
    if (pthread_mutex_unlock(&mtx[shard]) != 0) {
        *error = 1;
        return 0;
    }

    return now | lower_bits;
}

// validates the mysql inputs from shard_id_to_ms
my_bool shard_id_to_ms_init(
        UDF_INIT * const initid,
        UDF_ARGS * args,
        char * const msg ) {
    if (args->arg_count != 1) {
        snprintf(msg, MYSQL_ERRMSG_SIZE, "Usage: shard_id_to_ms(<id>)");
        return 1;
    }

    args->arg_type[0] = INT_RESULT;
    initid->maybe_null = 0;
    initid->const_item = 0;
    initid->ptr = NULL;
    return 0;
}

// called by mysql when shard_id_to_ms(<id>) is evaluated
// @return 41-bit integer representing ms since epoch (1970-01-01)
uint64_t shard_id_to_ms(
        UDF_INIT *initid __attribute__((unused)),
        UDF_ARGS *args,
        char *is_null __attribute__((unused)),
        char *error __attribute__((unused)) ) {

    uint64_t ts    = *((uint64_t *)args->args[0]) >> 23;
    //these shifts and multiplies are only needed when get_msec_epoch is used
    uint64_t secs  = (ts >> 10) * 1000;
    uint64_t msecs = ((ts & MAX_SHARD_VALUE) << 10) / 1000;

    return secs + msecs + INTERNAL_EPOCH_MS;
}
