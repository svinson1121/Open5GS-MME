#include <stdbool.h>
#include <stdint.h>

void mme_redis_init(void);
void mme_redis_final(void);

bool redis_set_ue_ip_lease(const char* imsi_bcd, const char* apn, uint32_t ipv4);
bool redis_get_ue_ip_lease(const char* imsi_bcd, const char* apn, uint32_t* ipv4_out);
bool redis_is_messgae_dup(uint8_t *buf, size_t buf_sz);