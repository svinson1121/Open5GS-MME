#ifndef OGS_DNS_RESOLVERS_H
#define OGS_DNS_RESOLVERS_H

#include "core/ogs-core.h"

#define OGS_DNS_RESOLVERS_INSIDE

#ifdef __cplusplus
extern "C" {
#endif

#undef OGS_DNS_RESOLVERS_INSIDE

extern int __ogs_dns_resolvers_domain;

#undef OGS_LOG_DOMAIN
#define OGS_LOG_DOMAIN __ogs_dns_resolvers_domain

#ifdef __cplusplus
}
#endif

#endif /* OGS_DNS_RESOLVERS_H */