#ifndef DNS_RESOLVERS_H
#define DNS_RESOLVERS_H

#include <stdlib.h>
#include <stdbool.h>


enum { DNS_RESOLVERS_MAX_TARGET_STR = 8,
       DNS_RESOLVERS_MAX_INTERFACE_STR = 8,
       DNS_RESOLVERS_MAX_PROTOCOL_STR = 8,
       DNS_RESOLVERS_MAX_APN_STR = 32,
       DNS_RESOLVERS_MAX_MNC_STR = 8,
       DNS_RESOLVERS_MAX_MCC_STR = 8,
       DNS_RESOLVERS_MAX_DOMAIN_SUFFIX_STR = 64,
       DNS_RESOLVERS_MAX_DOMAIN_NAME_STR = 128, };


typedef struct {
    char target[DNS_RESOLVERS_MAX_TARGET_STR];
    char interface[DNS_RESOLVERS_MAX_INTERFACE_STR];
    char protocol[DNS_RESOLVERS_MAX_PROTOCOL_STR];
    char apn[DNS_RESOLVERS_MAX_APN_STR];
    char mnc[DNS_RESOLVERS_MAX_MNC_STR];
    char mcc[DNS_RESOLVERS_MAX_MCC_STR];
    char domain_suffix[DNS_RESOLVERS_MAX_DOMAIN_SUFFIX_STR];

    /* Used internally */
    char _domain_name[DNS_RESOLVERS_MAX_DOMAIN_NAME_STR];
} ResolverContext;

/* 
 * Example:
 *   Input:
 *     context->target = "pgw"
 *     context->interface = "s5"
 *     context->protocol = "gtp"
 *     context->apn = "mms"
 *     context->mnc = "001"
 *     context->mcc = "100"
 *     context->domain_suffix = "3gppnetwork.org.nickvsnetworking.com";
 *     buf = ""
 *     buf_sz = 16
 *   Output:
 *     Result = true
 *     buf = "172.20.14.55"
 * 
 * Note:
 *   This will preform a NAPTR lookup, filter out answers that 
 *   don't support our desired service, sort the answers, then
 *   go through each of the answers until we obtain an IPv4 address
 *   that we return.
 *   If we cannot obtain an IP address we return false and do not 
 *   change buf.
 */
bool resolve_naptr(ResolverContext * const context, char *buf, size_t buf_sz);

#endif /* DNS_RESOLVERS_H */