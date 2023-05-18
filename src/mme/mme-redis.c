#include "ogs-redis.h"
#include "mme-redis.h"
#include "mme-context.h"


static redisContext* connection = NULL;

void mme_redis_init(void) {
    connection = ogs_redis_initialise(
        mme_self()->redis_server_config.address,
        mme_self()->redis_server_config.port
    );
}

void mme_redis_final(void) {
    ogs_redis_finalise(connection);
}

bool redis_set_ue_ip_lease(const char* imsi_bcd, const char* apn, uint32_t ipv4) {
    bool isSuccess = false;
    
    if ((NULL != imsi_bcd) &&
        (NULL != apn)) {
        /* Set the lease duration for this ue */
        redisReply *reply = redisCommand(connection,
            "SET [%s|%s] EX %i",
            imsi_bcd,
            apn,
            mme_self()->redis_ip_reuse.expire_time_sec
        );
        ogs_info(
            "SET [%s|%s] EX %i",
            imsi_bcd,
            apn,
            mme_self()->redis_ip_reuse.expire_time_sec
        );

        if (NULL == reply) {
            return false;
        }

        isSuccess = true;
        freeReplyObject(reply);
    }

    return isSuccess;
}

bool redis_get_ue_ip_lease(const char* imsi_bcd, const char* apn, uint32_t* ipv4_out) {
    bool isSuccess = false;
    
    if ((NULL != imsi_bcd) &&
        (NULL != apn)      &&
        (NULL != ipv4_out)) {
        /* Get recently used IP if there is one */
        redisReply *reply = redisCommand(connection, "GET [%s|%s]", imsi_bcd, apn);
        ogs_info("GET [%s|%s]", imsi_bcd, apn);

        if (NULL == reply) {
            return false;
        }

        if (REDIS_REPLY_STRING == reply->type) {
            /* We can only SET integers as strings using hiredis so we need
            * to convert the string integer back to an uint32_t */
            *ipv4_out = (uint32_t)atoi(reply->str);
            isSuccess = true;
        }

        freeReplyObject(reply);
    }

    return isSuccess;
}

bool redis_is_messgae_dup(uint8_t *buf, size_t buf_sz) {
    bool is_dup = true;
    redisReply *reply = NULL;

    /* Have we seen this exact message recently? */
    reply = redisCommand(connection, "GET %b", buf, buf_sz);

    if (NULL == reply) {
        ogs_error("Failed to get a reply from redis server");
        return false;
    }

    if (reply->type != REDIS_REPLY_NIL) {
        ogs_debug("S1AP message was a duplicate");
        is_dup = true;
    }
    else {
        ogs_debug("S1AP message was not a duplicate");
        is_dup = false;
    }
    freeReplyObject(reply);

    /* Tell redis to remember this message for expire_time_sec */
    reply = redisCommand(connection, "INCR %b", buf, buf_sz);
    freeReplyObject(reply);
    reply = redisCommand(connection, "EXPIRE %b %i", buf, buf_sz, mme_self()->redis_dup_detection.expire_time_sec);
    freeReplyObject(reply);

    return is_dup;
}