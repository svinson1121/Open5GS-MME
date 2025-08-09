/*
 * Copyright (C) 2019-2023 by Sukchan Lee <acetcom@gmail.com>
 *
 * This file is part of Open5GS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ogs-sctp.h"

#include "mme-context.h"
#include "mme-event.h"
#include "mme-timer.h"
#include "nas-path.h"
#include "s1ap-path.h"
#include "s1ap-handler.h"
#include "sbcap-path.h"
#include "sbcap-handler.h"
#include "mme-sm.h"
#include "mme-gtp-path.h"
#include "dns_resolvers.h"

#define MAX_CELL_PER_ENB            8

static mme_context_t self;
static ogs_diam_config_t g_diam_conf;

int __mme_log_domain;
int __emm_log_domain;
int __esm_log_domain;
int __ogs_dns_resolvers_domain;

static OGS_POOL(mme_sgw_pool, mme_sgw_t);
static OGS_POOL(mme_pgw_pool, mme_pgw_t);
static OGS_POOL(mme_vlr_pool, mme_vlr_t);
static OGS_POOL(mme_csmap_pool, mme_csmap_t);
static OGS_POOL(mme_hssmap_pool, mme_hssmap_t);

static OGS_POOL(mme_enb_pool, mme_enb_t);
static OGS_POOL(mme_ue_pool, mme_ue_t);
static OGS_POOL(mme_s11_teid_pool, ogs_pool_id_t);
static OGS_POOL(enb_ue_pool, enb_ue_t);
static OGS_POOL(sgw_ue_pool, sgw_ue_t);
static OGS_POOL(mme_sess_pool, mme_sess_t);
static OGS_POOL(mme_bearer_pool, mme_bearer_t);

static OGS_POOL(m_tmsi_pool, mme_m_tmsi_t);

static int context_initialized = 0;

static int num_of_enb_ue = 0;
static int num_of_mme_sess = 0;

static void stats_add_enb_ue(void);
static void stats_remove_enb_ue(void);
static void stats_add_mme_session(void);
static void stats_remove_mme_session(void);

static bool compare_ue_info(mme_sgw_t *node, enb_ue_t *enb_ue);
static mme_sgw_t *selected_sgw_node(mme_sgw_t *current, enb_ue_t *enb_ue);
static mme_sgw_t *changed_sgw_node(mme_sgw_t *current, enb_ue_t *enb_ue);

static int rand_under(int val);

void mme_context_init(void)
{
    ogs_assert(context_initialized == 0);

    /* Initial FreeDiameter Config */
    memset(&g_diam_conf, 0, sizeof(ogs_diam_config_t));

    /* Initialize MME context */
    memset(&self, 0, sizeof(mme_context_t));
    self.diam_config = &g_diam_conf;

    ogs_log_install_domain(&__ogs_sctp_domain, "sctp", ogs_core()->log.level);
    ogs_log_install_domain(&__ogs_s1ap_domain, "s1ap", ogs_core()->log.level);
    ogs_log_install_domain(&__ogs_sbcap_domain, "sbcap", ogs_core()->log.level);
    ogs_log_install_domain(&__ogs_nas_domain, "nas", ogs_core()->log.level);
    ogs_log_install_domain(&__ogs_diam_domain, "diam", ogs_core()->log.level);
    ogs_log_install_domain(&__mme_log_domain, "mme", ogs_core()->log.level);
    ogs_log_install_domain(&__emm_log_domain, "emm", ogs_core()->log.level);
    ogs_log_install_domain(&__esm_log_domain, "esm", ogs_core()->log.level);
    ogs_log_install_domain(&__ogs_dns_resolvers_domain, "dns_resolvers", ogs_core()->log.level);

    ogs_list_init(&self.s1ap_list);
    ogs_list_init(&self.s1ap_list6);

    ogs_list_init(&self.sgw_list);
    ogs_list_init(&self.sgw_roaming_list);
    ogs_list_init(&self.pgw_list);
    ogs_list_init(&self.enb_list);
    ogs_list_init(&self.vlr_list);
    ogs_list_init(&self.csmap_list);
    ogs_list_init(&self.hssmap_list);

    ogs_pool_init(&mme_sgw_pool, ogs_app()->pool.nf);
    ogs_pool_init(&mme_pgw_pool, ogs_app()->pool.nf);
    ogs_pool_init(&mme_vlr_pool, ogs_app()->pool.nf);
    ogs_pool_init(&mme_csmap_pool, ogs_app()->pool.csmap);
    ogs_pool_init(&mme_hssmap_pool, ogs_app()->pool.nf);

    /* Allocate TWICE the pool to check if maximum number of eNBs is reached */
    ogs_pool_init(&mme_enb_pool, ogs_app()->max.peer*2);

    ogs_pool_init(&mme_ue_pool, ogs_app()->max.ue);
    ogs_pool_init(&mme_s11_teid_pool, ogs_app()->max.ue);
    ogs_pool_random_id_generate(&mme_s11_teid_pool);

    ogs_pool_init(&enb_ue_pool, ogs_app()->max.ue);
    ogs_pool_init(&sgw_ue_pool, ogs_app()->max.ue);
    ogs_pool_init(&mme_sess_pool, ogs_app()->pool.sess);
    ogs_pool_init(&mme_bearer_pool, ogs_app()->pool.bearer);
    ogs_pool_init(&m_tmsi_pool, ogs_app()->max.ue*2);
    ogs_pool_random_id_generate(&m_tmsi_pool);

    self.enb_addr_hash = ogs_hash_make();
    ogs_assert(self.enb_addr_hash);
    self.enb_id_hash = ogs_hash_make();
    ogs_assert(self.enb_id_hash);
    self.imsi_ue_hash = ogs_hash_make();
    ogs_assert(self.imsi_ue_hash);
    self.guti_ue_hash = ogs_hash_make();
    ogs_assert(self.guti_ue_hash);
    self.mme_s11_teid_hash = ogs_hash_make();
    ogs_assert(self.mme_s11_teid_hash);

    ogs_list_init(&self.mme_ue_list);

    context_initialized = 1;
}

void mme_context_final(void)
{
    ogs_assert(context_initialized == 1);

    mme_enb_remove_all();
    mme_ue_remove_all();

    mme_sgw_remove_all();
    mme_sgw_roaming_remove_all();
    mme_pgw_remove_all();
    mme_csmap_remove_all();
    mme_vlr_remove_all();

    ogs_assert(self.enb_addr_hash);
    ogs_hash_destroy(self.enb_addr_hash);
    ogs_assert(self.enb_id_hash);
    ogs_hash_destroy(self.enb_id_hash);

    ogs_assert(self.imsi_ue_hash);
    ogs_hash_destroy(self.imsi_ue_hash);
    ogs_assert(self.guti_ue_hash);
    ogs_hash_destroy(self.guti_ue_hash);
    ogs_assert(self.mme_s11_teid_hash);
    ogs_hash_destroy(self.mme_s11_teid_hash);

    ogs_pool_final(&m_tmsi_pool);
    ogs_pool_final(&mme_bearer_pool);
    ogs_pool_final(&mme_sess_pool);
    ogs_pool_final(&mme_ue_pool);
    ogs_pool_final(&mme_s11_teid_pool);
    ogs_pool_final(&enb_ue_pool);
    ogs_pool_final(&sgw_ue_pool);

    ogs_pool_final(&mme_enb_pool);

    ogs_pool_final(&mme_sgw_pool);
    ogs_pool_final(&mme_pgw_pool);
    ogs_pool_final(&mme_csmap_pool);
    ogs_pool_final(&mme_vlr_pool);

    context_initialized = 0;
}

mme_context_t *mme_self(void)
{
    return &self;
}

static int mme_context_prepare(void)
{
    self.relative_capacity = 0xff;

    self.s1ap_port = OGS_S1AP_SCTP_PORT;
    self.sbcap_port = OGS_SBCAP_SCTP_PORT;
    self.sgsap_port = OGS_SGSAP_SCTP_PORT;
    self.diam_config->cnf_port = DIAMETER_PORT;
    self.diam_config->cnf_port_tls = DIAMETER_SECURE_PORT;

    return OGS_OK;
}

static int mme_context_validation(void)
{
    ogs_nas_gprs_timer_t gprs_timer;

    if (self.diam_conf_path == NULL &&
        (self.diam_config->cnf_diamid == NULL ||
        self.diam_config->cnf_diamrlm == NULL ||
        self.diam_config->cnf_addr == NULL)) {
        ogs_error("No mme.freeDiameter in '%s'", ogs_app()->file);
        return OGS_ERROR;
    }

    if (ogs_list_first(&self.s1ap_list) == NULL &&
        ogs_list_first(&self.s1ap_list6) == NULL) {
        ogs_error("No mme.s1ap in '%s'", ogs_app()->file);
        return OGS_RETRY;
    }

    if (ogs_list_first(&self.sbcap_list) == NULL &&
        ogs_list_first(&self.sbcap_list6) == NULL) {
        ogs_error("No mme.sbc in '%s'", ogs_app()->file);
    }

    if (ogs_list_first(&ogs_gtp_self()->gtpc_list) == NULL &&
        ogs_list_first(&ogs_gtp_self()->gtpc_list6) == NULL) {
        ogs_error("No mme.gtpc in '%s'", ogs_app()->file);
        return OGS_RETRY;
    }

    if (ogs_list_first(&self.sgw_list) == NULL) {
        ogs_error("No sgw.gtpc in '%s'", ogs_app()->file);
        return OGS_ERROR;
    }

    if (ogs_list_first(&self.sgw_roaming_list) == NULL) {
        ogs_warn("No sgw_roaming.gtpc in '%s'", ogs_app()->file);
    }

    if (ogs_list_first(&self.pgw_list) == NULL) {
        ogs_error("No pgw.gtpc in '%s'", ogs_app()->file);
        return OGS_ERROR;
    }

    if (self.max_num_of_served_gummei == 0) {
        ogs_error("No mme.gummei in '%s'", ogs_app()->file);
        return OGS_ERROR;
    }

    if (self.served_gummei[0].num_of_plmn_id == 0) {
        ogs_error("No mme.gummei.plmn_id in '%s'", ogs_app()->file);
        return OGS_ERROR;
    }

    if (self.served_gummei[0].num_of_mme_gid == 0) {
        ogs_error("No mme.gummei.mme_gid in '%s'", ogs_app()->file);
        return OGS_ERROR;
    }

    if (self.served_gummei[0].num_of_mme_code == 0) {
        ogs_error("No mme.gummei.mme_code in '%s'", ogs_app()->file);
        return OGS_ERROR;
    }

    if (self.num_of_served_tai == 0) {
        ogs_error("No mme.tai in '%s'", ogs_app()->file);
        return OGS_ERROR;
    }

    if (self.served_tai[0].list0.tai[0].num == 0 &&
        self.served_tai[0].list2.num == 0) {
        ogs_error("No mme.tai.plmn_id|tac in '%s'", ogs_app()->file);
        return OGS_ERROR;
    }

    if (self.num_of_integrity_order == 0) {
        ogs_error("No mme.security.integrity_order in '%s'",
                ogs_app()->file);
        return OGS_ERROR;
    }
    if (self.num_of_ciphering_order == 0) {
        ogs_error("no mme.security.ciphering_order in '%s'",
                ogs_app()->file);
        return OGS_ERROR;
    }
    if (ogs_nas_gprs_timer_from_sec(&gprs_timer, self.time.t3402.value) !=
        OGS_OK) {
        ogs_error("Not support GPRS Timer [%d]", (int)self.time.t3402.value);
        return OGS_ERROR;
    }
    if (ogs_nas_gprs_timer_from_sec(&gprs_timer, self.time.t3412.value) !=
        OGS_OK) {
        ogs_error("Not support GPRS Timer [%d]", (int)self.time.t3412.value);
        return OGS_ERROR;
    }
    if (ogs_nas_gprs_timer_from_sec(&gprs_timer, self.time.t3423.value) !=
        OGS_OK) {
        ogs_error("Not support GPRS Timer [%d]", (int)self.time.t3423.value);
        return OGS_ERROR;
    }

    return OGS_OK;
}

int mme_context_parse_config(void)
{
    int rv;
    yaml_document_t *document = NULL;
    ogs_yaml_iter_t root_iter;

    document = ogs_app()->document;
    ogs_assert(document);

    rv = mme_context_prepare();
    if (rv != OGS_OK) return rv;

    ogs_yaml_iter_init(&root_iter, document);
    while (ogs_yaml_iter_next(&root_iter)) {
        const char *root_key = ogs_yaml_iter_key(&root_iter);
        ogs_assert(root_key);
        if (!strcmp(root_key, "mme")) {
            ogs_yaml_iter_t mme_iter;
            ogs_yaml_iter_recurse(&root_iter, &mme_iter);
            while (ogs_yaml_iter_next(&mme_iter)) {
                const char *mme_key = ogs_yaml_iter_key(&mme_iter);
                ogs_assert(mme_key);
                if (!strcmp(mme_key, "freeDiameter")) {
                    yaml_node_t *node =
                        yaml_document_get_node(document, mme_iter.pair->value);
                    ogs_assert(node);
                    if (node->type == YAML_SCALAR_NODE) {
                        self.diam_conf_path = ogs_yaml_iter_value(&mme_iter);
                    } else if (node->type == YAML_MAPPING_NODE) {
                        ogs_yaml_iter_t fd_iter;
                        ogs_yaml_iter_recurse(&mme_iter, &fd_iter);

                        while (ogs_yaml_iter_next(&fd_iter)) {
                            const char *fd_key = ogs_yaml_iter_key(&fd_iter);
                            ogs_assert(fd_key);
                            if (!strcmp(fd_key, "identity")) {
                                self.diam_config->cnf_diamid =
                                    ogs_yaml_iter_value(&fd_iter);
                            } else if (!strcmp(fd_key, "realm")) {
                                self.diam_config->cnf_diamrlm =
                                    ogs_yaml_iter_value(&fd_iter);
                            } else if (!strcmp(fd_key, "port")) {
                                const char *v = ogs_yaml_iter_value(&fd_iter);
                                if (v) self.diam_config->cnf_port = atoi(v);
                            } else if (!strcmp(fd_key, "sec_port")) {
                                const char *v = ogs_yaml_iter_value(&fd_iter);
                                if (v) self.diam_config->cnf_port_tls = atoi(v);
                            } else if (!strcmp(fd_key, "listen_on")) {
                                self.diam_config->cnf_addr =
                                    ogs_yaml_iter_value(&fd_iter);
                            } else if (!strcmp(fd_key, "no_fwd")) {
                                self.diam_config->cnf_flags.no_fwd =
                                    ogs_yaml_iter_bool(&fd_iter);
                            } else if (!strcmp(fd_key, "load_extension")) {
                                ogs_yaml_iter_t ext_array, ext_iter;
                                ogs_yaml_iter_recurse(&fd_iter, &ext_array);
                                do {
                                    const char *module = NULL;
                                    const char *conf = NULL;

                                    if (ogs_yaml_iter_type(&ext_array) ==
                                        YAML_MAPPING_NODE) {
                                        memcpy(&ext_iter, &ext_array,
                                                sizeof(ogs_yaml_iter_t));
                                    } else if (ogs_yaml_iter_type(&ext_array) ==
                                        YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&ext_array))
                                            break;
                                        ogs_yaml_iter_recurse(
                                                &ext_array, &ext_iter);
                                    } else if (ogs_yaml_iter_type(&ext_array) ==
                                        YAML_SCALAR_NODE) {
                                        break;
                                    } else
                                        ogs_assert_if_reached();

                                    while (ogs_yaml_iter_next(&ext_iter)) {
                                        const char *ext_key =
                                            ogs_yaml_iter_key(&ext_iter);
                                        ogs_assert(ext_key);
                                        if (!strcmp(ext_key, "module")) {
                                            module =
                                                ogs_yaml_iter_value(&ext_iter);
                                        } else if (!strcmp(ext_key, "conf")) {
                                            conf =
                                                ogs_yaml_iter_value(&ext_iter);
                                        } else
                                            ogs_warn("unknown key `%s`",
                                                    ext_key);
                                    }

                                    if (module) {
                                        self.diam_config->
                                            ext[self.diam_config->num_of_ext].
                                                module = module;
                                        self.diam_config->
                                            ext[self.diam_config->num_of_ext].
                                                conf = conf;
                                        self.diam_config->num_of_ext++;
                                    }
                                } while (ogs_yaml_iter_type(&ext_array) ==
                                        YAML_SEQUENCE_NODE);
                            } else if (!strcmp(fd_key, "connect")) {
                                ogs_yaml_iter_t conn_array, conn_iter;
                                ogs_yaml_iter_recurse(&fd_iter, &conn_array);
                                do {
                                    const char *identity = NULL;
                                    const char *addr = NULL;
                                    uint16_t port = 0;

                                    if (ogs_yaml_iter_type(&conn_array) ==
                                        YAML_MAPPING_NODE) {
                                        memcpy(&conn_iter, &conn_array,
                                                sizeof(ogs_yaml_iter_t));
                                    } else if (ogs_yaml_iter_type(
                                        &conn_array) == YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&conn_array))
                                            break;
                                        ogs_yaml_iter_recurse(
                                                &conn_array, &conn_iter);
                                    } else if (ogs_yaml_iter_type(
                                        &conn_array) == YAML_SCALAR_NODE) {
                                        break;
                                    } else
                                        ogs_assert_if_reached();

                                    while (ogs_yaml_iter_next(&conn_iter)) {
                                        const char *conn_key =
                                            ogs_yaml_iter_key(&conn_iter);
                                        ogs_assert(conn_key);
                                        if (!strcmp(conn_key, "identity")) {
                                            identity =
                                                ogs_yaml_iter_value(&conn_iter);
                                        } else if (!strcmp(conn_key, "addr")) {
                                            addr =
                                                ogs_yaml_iter_value(&conn_iter);
                                        } else if (!strcmp(conn_key, "port")) {
                                            const char *v =
                                                ogs_yaml_iter_value(&conn_iter);
                                            if (v) port = atoi(v);
                                        } else
                                            ogs_warn("unknown key `%s`",
                                                    conn_key);
                                    }

                                    if (identity && addr) {
                                        self.diam_config->
                                            conn[self.diam_config->num_of_conn].
                                                identity = identity;
                                        self.diam_config->
                                            conn[self.diam_config->num_of_conn].
                                                addr = addr;
                                        self.diam_config->
                                            conn[self.diam_config->num_of_conn].
                                                port = port;
                                        self.diam_config->num_of_conn++;
                                    }
                                } while (ogs_yaml_iter_type(&conn_array) ==
                                        YAML_SEQUENCE_NODE);
                            } else
                                ogs_warn("unknown key `%s`", fd_key);
                        }
                    }
                } else if (!strcmp(mme_key, "relative_capacity")) {
                    const char *v = ogs_yaml_iter_value(&mme_iter);
                    if (v) self.relative_capacity = atoi(v);
                } else if (!strcmp(mme_key, "s1ap")) {
                    ogs_yaml_iter_t s1ap_array, s1ap_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &s1ap_array);
                    do {
                        int family = AF_UNSPEC;
                        int i, num = 0;
                        const char *hostname[OGS_MAX_NUM_OF_HOSTNAME];
                        uint16_t port = self.s1ap_port;
                        const char *dev = NULL;
                        ogs_sockaddr_t *addr = NULL;

                        ogs_sockopt_t option;
                        bool is_option = false;

                        if (ogs_yaml_iter_type(&s1ap_array) ==
                                YAML_MAPPING_NODE) {
                            memcpy(&s1ap_iter, &s1ap_array,
                                    sizeof(ogs_yaml_iter_t));
                        } else if (ogs_yaml_iter_type(&s1ap_array) ==
                            YAML_SEQUENCE_NODE) {
                            if (!ogs_yaml_iter_next(&s1ap_array))
                                break;
                            ogs_yaml_iter_recurse(&s1ap_array, &s1ap_iter);
                        } else if (ogs_yaml_iter_type(&s1ap_array) ==
                            YAML_SCALAR_NODE) {
                            break;
                        } else
                            ogs_assert_if_reached();

                        while (ogs_yaml_iter_next(&s1ap_iter)) {
                            const char *s1ap_key =
                                ogs_yaml_iter_key(&s1ap_iter);
                            ogs_assert(s1ap_key);
                            if (!strcmp(s1ap_key, "family")) {
                                const char *v = ogs_yaml_iter_value(&s1ap_iter);
                                if (v) family = atoi(v);
                                if (family != AF_UNSPEC &&
                                    family != AF_INET && family != AF_INET6) {
                                    ogs_warn("Ignore family(%d) : "
                                        "AF_UNSPEC(%d), "
                                        "AF_INET(%d), AF_INET6(%d) ",
                                        family, AF_UNSPEC, AF_INET, AF_INET6);
                                    family = AF_UNSPEC;
                                }
                            } else if (!strcmp(s1ap_key, "addr") ||
                                    !strcmp(s1ap_key, "name")) {
                                ogs_yaml_iter_t hostname_iter;
                                ogs_yaml_iter_recurse(
                                        &s1ap_iter, &hostname_iter);
                                ogs_assert(ogs_yaml_iter_type(&hostname_iter) !=
                                    YAML_MAPPING_NODE);

                                do {
                                    if (ogs_yaml_iter_type(&hostname_iter) ==
                                            YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&hostname_iter))
                                            break;
                                    }

                                    ogs_assert(num < OGS_MAX_NUM_OF_HOSTNAME);
                                    hostname[num++] =
                                        ogs_yaml_iter_value(&hostname_iter);
                                } while (
                                    ogs_yaml_iter_type(&hostname_iter) ==
                                        YAML_SEQUENCE_NODE);
                            } else if (!strcmp(s1ap_key, "port")) {
                                const char *v = ogs_yaml_iter_value(&s1ap_iter);
                                if (v) port = atoi(v);
                            } else if (!strcmp(s1ap_key, "dev")) {
                                dev = ogs_yaml_iter_value(&s1ap_iter);
                            } else if (!strcmp(s1ap_key, "option")) {
                                rv = ogs_app_config_parse_sockopt(
                                        &s1ap_iter, &option);
                                if (rv != OGS_OK) return rv;
                                is_option = true;
                            } else
                                ogs_warn("unknown key `%s`", s1ap_key);
                        }

                        addr = NULL;
                        for (i = 0; i < num; i++) {
                            rv = ogs_addaddrinfo(&addr,
                                    family, hostname[i], port, 0);
                            ogs_assert(rv == OGS_OK);
                        }

                        if (addr) {
                            if (ogs_app()->parameter.no_ipv4 == 0)
                                ogs_socknode_add(
                                    &self.s1ap_list, AF_INET, addr,
                                    is_option ? &option : NULL);
                            if (ogs_app()->parameter.no_ipv6 == 0)
                                ogs_socknode_add(
                                    &self.s1ap_list6, AF_INET6, addr,
                                    is_option ? &option : NULL);
                            ogs_freeaddrinfo(addr);
                        }

                        if (dev) {
                            rv = ogs_socknode_probe(
                                    ogs_app()->parameter.no_ipv4 ?
                                        NULL : &self.s1ap_list,
                                    ogs_app()->parameter.no_ipv6 ?
                                        NULL : &self.s1ap_list6,
                                    dev, port,
                                    is_option ? &option : NULL);
                            ogs_assert(rv == OGS_OK);
                        }

                    } while (ogs_yaml_iter_type(&s1ap_array) ==
                            YAML_SEQUENCE_NODE);

                    if (ogs_list_first(&self.s1ap_list) == NULL &&
                        ogs_list_first(&self.s1ap_list6) == NULL) {
                        rv = ogs_socknode_probe(
                                ogs_app()->parameter.no_ipv4 ?
                                    NULL : &self.s1ap_list,
                                ogs_app()->parameter.no_ipv6 ?
                                    NULL : &self.s1ap_list6,
                                NULL, self.s1ap_port, NULL);
                        ogs_assert(rv == OGS_OK);
                    }
                } else if (!strcmp(mme_key, "cbc")) {
                    ogs_yaml_iter_t cbc_array, cbc_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &cbc_array);
                    do {
                        int family = AF_UNSPEC;
                        int i, num = 0;
                        const char *hostname[OGS_MAX_NUM_OF_HOSTNAME];
                        uint16_t port = self.sbcap_port;
                        const char *dev = NULL;
                        ogs_sockaddr_t *addr = NULL;

                        ogs_sockopt_t option;
                        bool is_option = false;

                        if (ogs_yaml_iter_type(&cbc_array) ==
                                YAML_MAPPING_NODE) {
                            memcpy(&cbc_iter, &cbc_array,
                                    sizeof(ogs_yaml_iter_t));
                        } else if (ogs_yaml_iter_type(&cbc_array) ==
                            YAML_SEQUENCE_NODE) {
                            if (!ogs_yaml_iter_next(&cbc_array))
                                break;
                            ogs_yaml_iter_recurse(&cbc_array, &cbc_iter);
                        } else if (ogs_yaml_iter_type(&cbc_array) ==
                            YAML_SCALAR_NODE) {
                            break;
                        } else
                            ogs_assert_if_reached();

                        while (ogs_yaml_iter_next(&cbc_iter)) {
                            const char *cbc_key =
                                ogs_yaml_iter_key(&cbc_iter);
                            ogs_assert(cbc_key);
                            if (!strcmp(cbc_key, "family")) {
                                const char *v = ogs_yaml_iter_value(&cbc_iter);
                                if (v) family = atoi(v);
                                if (family != AF_UNSPEC &&
                                    family != AF_INET && family != AF_INET6) {
                                    ogs_warn("Ignore family(%d) : "
                                        "AF_UNSPEC(%d), "
                                        "AF_INET(%d), AF_INET6(%d) ",
                                        family, AF_UNSPEC, AF_INET, AF_INET6);
                                    family = AF_UNSPEC;
                                }
                            } else if (!strcmp(cbc_key, "addr") ||
                                    !strcmp(cbc_key, "name")) {
                                ogs_yaml_iter_t hostname_iter;
                                ogs_yaml_iter_recurse(
                                        &cbc_iter, &hostname_iter);
                                ogs_assert(ogs_yaml_iter_type(&hostname_iter) !=
                                    YAML_MAPPING_NODE);

                                do {
                                    if (ogs_yaml_iter_type(&hostname_iter) ==
                                            YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&hostname_iter))
                                            break;
                                    }

                                    ogs_assert(num < OGS_MAX_NUM_OF_HOSTNAME);
                                    hostname[num++] =
                                        ogs_yaml_iter_value(&hostname_iter);
                                } while (
                                    ogs_yaml_iter_type(&hostname_iter) ==
                                        YAML_SEQUENCE_NODE);
                            } else if (!strcmp(cbc_key, "port")) {
                                const char *v = ogs_yaml_iter_value(&cbc_iter);
                                if (v) port = atoi(v);
                            } else if (!strcmp(cbc_key, "dev")) {
                                dev = ogs_yaml_iter_value(&cbc_iter);
                            } else if (!strcmp(cbc_key, "option")) {
                                rv = ogs_app_config_parse_sockopt(
                                        &cbc_iter, &option);
                                if (rv != OGS_OK) return rv;
                                is_option = true;
                            } else
                                ogs_warn("unknown key `%s`", cbc_key);
                        }

                        addr = NULL;
                        for (i = 0; i < num; i++) {
                            rv = ogs_addaddrinfo(&addr,
                                    family, hostname[i], port, 0);
                            ogs_assert(rv == OGS_OK);
                        }

                        if (addr) {
                            if (ogs_app()->parameter.no_ipv4 == 0)
                                ogs_socknode_add(
                                    &self.sbcap_list, AF_INET, addr,
                                    is_option ? &option : NULL);
                            if (ogs_app()->parameter.no_ipv6 == 0)
                                ogs_socknode_add(
                                    &self.sbcap_list6, AF_INET6, addr,
                                    is_option ? &option : NULL);
                            ogs_freeaddrinfo(addr);
                        }

                        if (dev) {
                            rv = ogs_socknode_probe(
                                    ogs_app()->parameter.no_ipv4 ?
                                        NULL : &self.sbcap_list,
                                    ogs_app()->parameter.no_ipv6 ?
                                        NULL : &self.sbcap_list6,
                                    dev, port,
                                    is_option ? &option : NULL);
                            ogs_assert(rv == OGS_OK);
                        }

                    } while (ogs_yaml_iter_type(&cbc_array) ==
                            YAML_SEQUENCE_NODE);

                    if (ogs_list_first(&self.sbcap_list) == NULL &&
                        ogs_list_first(&self.sbcap_list6) == NULL) {
                        rv = ogs_socknode_probe(
                                ogs_app()->parameter.no_ipv4 ?
                                    NULL : &self.sbcap_list,
                                ogs_app()->parameter.no_ipv6 ?
                                    NULL : &self.sbcap_list6,
                                NULL, self.sbcap_port, NULL);
                        ogs_assert(rv == OGS_OK);
                    }
                } else if (!strcmp(mme_key, "gtpc")) {
                    /* handle config in gtp library */
                } else if (!strcmp(mme_key, "gummei")) {
                    ogs_yaml_iter_t gummei_array, gummei_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &gummei_array);
                    do {
                        served_gummei_t *gummei = NULL;
                        ogs_assert(self.max_num_of_served_gummei <
                                MAX_NUM_OF_SERVED_GUMMEI);
                        gummei = &self.served_gummei[
                            self.max_num_of_served_gummei];
                        ogs_assert(gummei);

                        if (ogs_yaml_iter_type(&gummei_array) ==
                                YAML_MAPPING_NODE) {
                            memcpy(&gummei_iter, &gummei_array,
                                    sizeof(ogs_yaml_iter_t));
                        } else if (ogs_yaml_iter_type(&gummei_array) ==
                            YAML_SEQUENCE_NODE) {
                            if (!ogs_yaml_iter_next(&gummei_array))
                                break;
                            ogs_yaml_iter_recurse(&gummei_array,
                                    &gummei_iter);
                        } else if (ogs_yaml_iter_type(&gummei_array) ==
                            YAML_SCALAR_NODE) {
                            break;
                        } else
                            ogs_assert_if_reached();

                        while (ogs_yaml_iter_next(&gummei_iter)) {
                            const char *gummei_key =
                                ogs_yaml_iter_key(&gummei_iter);
                            ogs_assert(gummei_key);
                            if (!strcmp(gummei_key, "plmn_id")) {
                                ogs_yaml_iter_t plmn_id_array, plmn_id_iter;
                                ogs_yaml_iter_recurse(&gummei_iter,
                                        &plmn_id_array);
                                do {
                                    ogs_plmn_id_t *plmn_id = NULL;
                                    const char *mcc = NULL, *mnc = NULL;
                                    ogs_assert(gummei->num_of_plmn_id <
                                            OGS_MAX_NUM_OF_PLMN);
                                    plmn_id = &gummei->plmn_id[
                                        gummei->num_of_plmn_id];
                                    ogs_assert(plmn_id);

                                    if (ogs_yaml_iter_type(&plmn_id_array) ==
                                            YAML_MAPPING_NODE) {
                                        memcpy(&plmn_id_iter, &plmn_id_array,
                                                sizeof(ogs_yaml_iter_t));
                                    } else if (ogs_yaml_iter_type(
                                                &plmn_id_array) ==
                                                YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&plmn_id_array))
                                            break;
                                        ogs_yaml_iter_recurse(&plmn_id_array,
                                                &plmn_id_iter);
                                    } else if (ogs_yaml_iter_type(
                                                &plmn_id_array) ==
                                                YAML_SCALAR_NODE) {
                                        break;
                                    } else
                                        ogs_assert_if_reached();

                                    while (ogs_yaml_iter_next(&plmn_id_iter)) {
                                        const char *plmn_id_key =
                                            ogs_yaml_iter_key(&plmn_id_iter);
                                        ogs_assert(plmn_id_key);
                                        if (!strcmp(plmn_id_key, "mcc")) {
                                            mcc = ogs_yaml_iter_value(
                                                    &plmn_id_iter);
                                        } else if (!strcmp(
                                                    plmn_id_key, "mnc")) {
                                            mnc = ogs_yaml_iter_value(
                                                    &plmn_id_iter);
                                        }
                                    }

                                    if (mcc && mnc) {
                                        ogs_plmn_id_build(plmn_id,
                                            atoi(mcc), atoi(mnc), strlen(mnc));
                                        gummei->num_of_plmn_id++;
                                    }

                                } while (ogs_yaml_iter_type(&plmn_id_array) ==
                                        YAML_SEQUENCE_NODE);
                            } else if (!strcmp(gummei_key, "mme_gid")) {
                                ogs_yaml_iter_t mme_gid_iter;
                                ogs_yaml_iter_recurse(
                                        &gummei_iter, &mme_gid_iter);
                                ogs_assert(ogs_yaml_iter_type(
                                        &mme_gid_iter) != YAML_MAPPING_NODE);

                                do {
                                    uint16_t *mme_gid = NULL;
                                    const char *v = NULL;

                                    ogs_assert(gummei->num_of_mme_gid <
                                            GRP_PER_MME);
                                    mme_gid = &gummei->mme_gid[
                                        gummei->num_of_mme_gid];
                                    ogs_assert(mme_gid);

                                    if (ogs_yaml_iter_type(&mme_gid_iter) ==
                                            YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&mme_gid_iter))
                                            break;
                                    }

                                    v = ogs_yaml_iter_value(&mme_gid_iter);
                                    if (v) {
                                        *mme_gid = atoi(v);
                                        gummei->num_of_mme_gid++;
                                    }
                                } while (
                                    ogs_yaml_iter_type(&mme_gid_iter) ==
                                        YAML_SEQUENCE_NODE);
                            } else if (!strcmp(gummei_key, "mme_code")) {
                                ogs_yaml_iter_t mme_code_iter;
                                ogs_yaml_iter_recurse(&gummei_iter,
                                        &mme_code_iter);
                                ogs_assert(ogs_yaml_iter_type(&mme_code_iter) !=
                                    YAML_MAPPING_NODE);

                                do {
                                    uint8_t *mme_code = NULL;
                                    const char *v = NULL;

                                    ogs_assert(gummei->num_of_mme_code <
                                            CODE_PER_MME);
                                    mme_code = &gummei->mme_code[
                                        gummei->num_of_mme_code];
                                    ogs_assert(mme_code);

                                    if (ogs_yaml_iter_type(&mme_code_iter) ==
                                            YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&mme_code_iter))
                                            break;
                                    }

                                    v = ogs_yaml_iter_value(&mme_code_iter);
                                    if (v) {
                                        *mme_code = atoi(v);
                                        gummei->num_of_mme_code++;
                                    }
                                } while (
                                    ogs_yaml_iter_type(&mme_code_iter) ==
                                        YAML_SEQUENCE_NODE);
                            } else
                                ogs_warn("unknown key `%s`", gummei_key);
                        }

                        if (gummei->num_of_plmn_id &&
                            gummei->num_of_mme_gid && gummei->num_of_mme_code) {
                            self.max_num_of_served_gummei++;
                        } else {
                            ogs_warn("Ignore gummei : "
                                    "plmn_id(%d), mme_gid(%d), mme_code(%d)",
                                gummei->num_of_plmn_id,
                                gummei->num_of_mme_gid,
                                gummei->num_of_mme_code);
                            gummei->num_of_plmn_id = 0;
                            gummei->num_of_mme_gid = 0;
                            gummei->num_of_mme_code = 0;
                        }
                    } while (ogs_yaml_iter_type(&gummei_array) ==
                            YAML_SEQUENCE_NODE);
                } else if (!strcmp(mme_key, "tai")) {
                    int num_of_list0 = 0;
                    int num_of_list1 = 0;
                    ogs_eps_tai0_list_t *list0 = NULL;
                    ogs_eps_tai1_list_t *list1 = NULL;
                    ogs_eps_tai2_list_t *list2 = NULL;

                    ogs_assert(self.num_of_served_tai <
                            OGS_MAX_NUM_OF_SERVED_TAI);
                    list0 = &self.served_tai[self.num_of_served_tai].list0;
                    list1 = &self.served_tai[self.num_of_served_tai].list1;
                    list2 = &self.served_tai[self.num_of_served_tai].list2;

                    ogs_yaml_iter_t tai_array, tai_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &tai_array);
                    do {
                        const char *mcc = NULL, *mnc = NULL;
                        int num_of_tac = 0;
                        uint16_t start[OGS_MAX_NUM_OF_TAI];
                        uint16_t end[OGS_MAX_NUM_OF_TAI];

                        if (ogs_yaml_iter_type(&tai_array) ==
                                YAML_MAPPING_NODE) {
                            memcpy(&tai_iter, &tai_array,
                                    sizeof(ogs_yaml_iter_t));
                        } else if (ogs_yaml_iter_type(&tai_array) ==
                            YAML_SEQUENCE_NODE) {
                            if (!ogs_yaml_iter_next(&tai_array))
                                break;
                            ogs_yaml_iter_recurse(&tai_array,
                                    &tai_iter);
                        } else if (ogs_yaml_iter_type(&tai_array) ==
                                YAML_SCALAR_NODE) {
                            break;
                        } else
                            ogs_assert_if_reached();

                        while (ogs_yaml_iter_next(&tai_iter)) {
                            const char *tai_key = ogs_yaml_iter_key(&tai_iter);
                            ogs_assert(tai_key);
                            if (!strcmp(tai_key, "plmn_id")) {
                                ogs_yaml_iter_t plmn_id_iter;

                                ogs_yaml_iter_recurse(&tai_iter, &plmn_id_iter);
                                while (ogs_yaml_iter_next(&plmn_id_iter)) {
                                    const char *plmn_id_key =
                                        ogs_yaml_iter_key(&plmn_id_iter);
                                    ogs_assert(plmn_id_key);
                                    if (!strcmp(plmn_id_key, "mcc")) {
                                        mcc = ogs_yaml_iter_value(
                                                &plmn_id_iter);
                                    } else if (!strcmp(plmn_id_key, "mnc")) {
                                        mnc = ogs_yaml_iter_value(
                                                &plmn_id_iter);
                                    }
                                }

                                if (mcc && mnc) {
                                    self.home_mnc_mcc[self.home_mnc_mcc_sz].mnc = atoi(mnc);
                                    self.home_mnc_mcc[self.home_mnc_mcc_sz].mcc = atoi(mcc);
                                    self.home_mnc_mcc_sz++;
                                }
                            } else if (!strcmp(tai_key, "tac")) {
                                ogs_yaml_iter_t tac_iter;
                                ogs_yaml_iter_recurse(&tai_iter, &tac_iter);
                                ogs_assert(ogs_yaml_iter_type(&tac_iter) !=
                                            YAML_MAPPING_NODE);
                                do {
                                    char *v = NULL;
                                    char *low = NULL, *high = NULL;

                                    if (ogs_yaml_iter_type(&tac_iter) ==
                                            YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&tac_iter))
                                            break;
                                    }

                                    v = (char *)ogs_yaml_iter_value(
                                                &tac_iter);
                                    if (v) {
                                        low = strsep(&v, "-");
                                        if (low && strlen(low) == 0)
                                            low = NULL;

                                        high = v;
                                        if (high && strlen(high) == 0)
                                            high = NULL;

                                        if (low) {
                                            ogs_assert(num_of_tac <
                                                OGS_MAX_NUM_OF_TAI);
                                            start[num_of_tac] = atoi(low);
                                            if (high) {
                                                end[num_of_tac] = atoi(high);
                                                if (end[num_of_tac] <
                                                    start[num_of_tac])
                                                    ogs_error(
                                                        "Invalid TAI range: "
                                                        "LOW:%s,HIGH:%s",
                                                            low, high);
                                                else if (
                                                    (end[num_of_tac]-
                                                    start[num_of_tac]+1) >
                                                        OGS_MAX_NUM_OF_TAI)
                                                    ogs_error(
                                                        "Overflow TAI range: "
                                                        "LOW:%s,HIGH:%s",
                                                            low, high);
                                                else
                                                    num_of_tac++;
                                            } else {
                                                end[num_of_tac] =
                                                    start[num_of_tac];
                                                num_of_tac++;
                                            }
                                        }
                                    }
                                } while (
                                    ogs_yaml_iter_type(&tac_iter) ==
                                        YAML_SEQUENCE_NODE);
                            } else
                                ogs_warn("unknown key `%s`", tai_key);
                        }

                        if (mcc && mnc && num_of_tac) {
                            if (num_of_tac == 1 && start[0] == end[0]) {
                                ogs_assert(list2->num < OGS_MAX_NUM_OF_TAI);

                                list2->type = OGS_TAI2_TYPE;

                                ogs_plmn_id_build(
                                    &list2->tai[list2->num].plmn_id,
                                    atoi(mcc), atoi(mnc), strlen(mnc));
                                list2->tai[list2->num].tac = start[0];

                                list2->num++;

                            } else {
                                int tac, count = 0;
                                for (tac = 0; tac < num_of_tac; tac++) {
                                    ogs_assert(end[tac] >= start[tac]);
                                    if (start[tac] == end[tac]) {
                                        ogs_assert(num_of_list0 <
                                                OGS_MAX_NUM_OF_TAI);

                                        list0->tai[num_of_list0].type =
                                            OGS_TAI0_TYPE;

                                        ogs_plmn_id_build(
                                            &list0->tai[num_of_list0].plmn_id,
                                            atoi(mcc), atoi(mnc), strlen(mnc));
                                        list0->tai[num_of_list0].
                                            tac[count] = start[tac];

                                        list0->tai[num_of_list0].num =
                                            ++count;

                                    } else if (start[tac] < end[tac]) {
                                        ogs_assert(num_of_list1 <
                                                OGS_MAX_NUM_OF_TAI);

                                        list1->tai[num_of_list1].type =
                                            OGS_TAI1_TYPE;

                                        ogs_plmn_id_build(
                                            &list1->tai[num_of_list1].plmn_id,
                                            atoi(mcc), atoi(mnc), strlen(mnc));
                                        list1->tai[num_of_list1].tac =
                                            start[tac];

                                        list1->tai[num_of_list1].num =
                                            end[tac]-start[tac]+1;

                                        num_of_list1++;
                                    }
                                }

                                if (count)
                                    num_of_list0++;
                            }
                        } else {
                            ogs_warn("Ignore tai : mcc(%p), mnc(%p), "
                                    "num_of_tac(%d)", mcc, mnc, num_of_tac);
                        }
                    } while (ogs_yaml_iter_type(&tai_array) ==
                            YAML_SEQUENCE_NODE);

                    if (list2->num || num_of_list1 || num_of_list0) {
                        self.num_of_served_tai++;
                    }
                } else if (!strcmp(mme_key, "tac-timezone")) {
                    ogs_yaml_iter_t tac_timezone_iter;

                    ogs_yaml_iter_recurse(&mme_iter, &tac_timezone_iter);
                    while (ogs_yaml_iter_next(&tac_timezone_iter)) {
                        const char *tac_timezone_key =
                            ogs_yaml_iter_key(&tac_timezone_iter);
                        ogs_assert(tac_timezone_key);

                        const char *gmt_timezone = ogs_yaml_iter_value(&tac_timezone_iter);
                        char *gmt_amount_str = strstr(gmt_timezone, "utc");
                        if (NULL == gmt_amount_str) {
                            gmt_amount_str = strstr(gmt_timezone, "UTC");
                        }
                        /* Minimum of 5 chars to make a valid UTC amount: "utc-1" "UTC+1" */
                        if ((NULL == gmt_amount_str) || (strlen(gmt_amount_str) < 5)) {
                            ogs_error("Timezone value for TAC '%s' was invalid ('%s'), expecting something like 'UTC+11:15' or 'utc-4'", tac_timezone_key, gmt_timezone);
                            continue;
                        }
                        /* Point to characters after "UTC" / "utc" */
                        gmt_amount_str += 3;

                        if (self.tac_timezone_map_sz < MAX_TAC_TIMEZONE_MAP_SZ) {
                            int gmt_modifier_sec = 0;

                            self.tac_timezone_map[self.tac_timezone_map_sz].tac = atoi(tac_timezone_key);
                            /* Add hours field */
                            gmt_modifier_sec = atoi(gmt_amount_str) * 60 * 60;

                            gmt_amount_str = strchr(gmt_amount_str, ':');
                            /* Resolution of 15 min so we are expecting exactly 
                             * 3 digits (inclusive of ':') when there are minutes */
                            if (gmt_amount_str != NULL) {
                                /* Move the pointer to the position after ':' */
                                ++gmt_amount_str;
                                /* Add minutes */
                                if (gmt_modifier_sec < 0) {
                                    gmt_modifier_sec -= atoi(gmt_amount_str) * 60;
                                } else {
                                    gmt_modifier_sec += atoi(gmt_amount_str) * 60;
                                }
                            }

                            self.tac_timezone_map[self.tac_timezone_map_sz].gmt_modifier_sec = gmt_modifier_sec;
                            
                            ogs_info(
                                "TAC of %i is associated with timezone UTC + %i seconds",
                                self.tac_timezone_map[self.tac_timezone_map_sz].tac,
                                self.tac_timezone_map[self.tac_timezone_map_sz].gmt_modifier_sec
                            );
                            self.tac_timezone_map_sz++;
                        } else {
                            ogs_error("Cannot have more than %i TAC Timezones specified", MAX_TAC_TIMEZONE_MAP_SZ);
                        }
                    }
                } else if (!strcmp(mme_key, "access_control")) {
                    ogs_yaml_iter_t access_control_array, access_control_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &access_control_array);
                    do {
                        ogs_assert(self.num_of_access_control <
                                OGS_MAX_NUM_OF_ACCESS_CONTROL);

                        if (ogs_yaml_iter_type(&access_control_array) ==
                                YAML_MAPPING_NODE) {
                            memcpy(&access_control_iter, &access_control_array,
                                    sizeof(ogs_yaml_iter_t));
                        } else if (ogs_yaml_iter_type(&access_control_array) ==
                            YAML_SEQUENCE_NODE) {
                            if (!ogs_yaml_iter_next(&access_control_array))
                                break;
                            ogs_yaml_iter_recurse(&access_control_array,
                                    &access_control_iter);
                        } else if (ogs_yaml_iter_type(&access_control_array) ==
                            YAML_SCALAR_NODE) {
                            break;
                        } else
                            ogs_assert_if_reached();

                        while (ogs_yaml_iter_next(&access_control_iter)) {
                            const char *mnc = NULL, *mcc = NULL;
                            int reject_cause = 0;
                            const char *access_control_key =
                                ogs_yaml_iter_key(&access_control_iter);
                            ogs_assert(access_control_key);
                            if (!strcmp(access_control_key,
                                        "default_reject_cause")) {
                                const char *v = ogs_yaml_iter_value(
                                        &access_control_iter);
                                if (v) self.default_reject_cause = atoi(v);
                            } else if (!strcmp(access_control_key, "plmn_id")) {
                                ogs_yaml_iter_t plmn_id_iter;

                                ogs_yaml_iter_recurse(&access_control_iter,
                                        &plmn_id_iter);
                                while (ogs_yaml_iter_next(&plmn_id_iter)) {
                                    const char *plmn_id_key =
                                        ogs_yaml_iter_key(&plmn_id_iter);
                                    ogs_assert(plmn_id_key);
                                    if (!strcmp(plmn_id_key, "reject_cause")) {
                                        const char *v = ogs_yaml_iter_value(
                                                &plmn_id_iter);
                                        if (v) reject_cause = atoi(v);
                                    } else if (!strcmp(plmn_id_key, "mcc")) {
                                        mcc = ogs_yaml_iter_value(
                                                &plmn_id_iter);
                                    } else if (!strcmp(plmn_id_key, "mnc")) {
                                        mnc = ogs_yaml_iter_value(
                                                &plmn_id_iter);
                                    }
                                }

                                if (mcc && mnc) {
                                    ogs_plmn_id_build(
                                        &self.access_control[
                                            self.num_of_access_control].
                                                plmn_id,
                                        atoi(mcc), atoi(mnc), strlen(mnc));
                                    if (reject_cause)
                                        self.access_control[
                                            self.num_of_access_control].
                                                reject_cause = reject_cause;
                                    self.num_of_access_control++;
                                }
                            } else
                                ogs_warn("unknown key `%s`",
                                        access_control_key);
                        }

                    } while (ogs_yaml_iter_type(&access_control_array) ==
                            YAML_SEQUENCE_NODE);
			} else if (!strcmp(mme_key, "hss_map")) {
                    ogs_yaml_iter_t hss_map_array, hss_map_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &hss_map_array);
                    do {
                        if (ogs_yaml_iter_type(&hss_map_array) ==
                                YAML_MAPPING_NODE) {
                            memcpy(&hss_map_iter, &hss_map_array,
                                    sizeof(ogs_yaml_iter_t));
                        } else if (ogs_yaml_iter_type(&hss_map_array) ==
                            YAML_SEQUENCE_NODE) {
                            if (!ogs_yaml_iter_next(&hss_map_array))
                                break;
                            ogs_yaml_iter_recurse(&hss_map_array,
                                    &hss_map_iter);
                        } else if (ogs_yaml_iter_type(&hss_map_array) ==
                            YAML_SCALAR_NODE) {
                            break;
                        } else
                            ogs_assert_if_reached();

                        while (ogs_yaml_iter_next(&hss_map_iter)) {
                            const char *mnc = NULL, *mcc = NULL, *realm = NULL, *host = NULL;
                            const char *hss_map_key =
                                ogs_yaml_iter_key(&hss_map_iter);
                            ogs_assert(hss_map_key);
                            if (!strcmp(hss_map_key, "plmn_id")) {
                                ogs_yaml_iter_t plmn_id_iter;

                                ogs_yaml_iter_recurse(&hss_map_iter,
                                        &plmn_id_iter);
                                while (ogs_yaml_iter_next(&plmn_id_iter)) {
                                    const char *plmn_id_key =
                                        ogs_yaml_iter_key(&plmn_id_iter);
                                    ogs_assert(plmn_id_key);
                                    if (!strcmp(plmn_id_key, "host")) {
                                        const char *v = ogs_yaml_iter_value(
                                                &plmn_id_iter);
                                        if (v) host = ogs_strndup(v, OGS_MAX_FQDN_LEN);
                                    } else if (!strcmp(plmn_id_key, "realm")) {
                                        const char *v = ogs_yaml_iter_value(
                                                &plmn_id_iter);
                                        if (v) realm = ogs_strndup(v, OGS_MAX_FQDN_LEN);
                                    } else if (!strcmp(plmn_id_key, "mcc")) {
                                        mcc = ogs_yaml_iter_value(
                                                &plmn_id_iter);
                                    } else if (!strcmp(plmn_id_key, "mnc")) {
                                        mnc = ogs_yaml_iter_value(
                                                &plmn_id_iter);
                                    }
                                }

                                if (mcc && mnc) {
                                    ogs_plmn_id_t plmn_id;
                                    mme_hssmap_t *hssmap = NULL;

                                    ogs_plmn_id_build(&plmn_id,
                                        atoi(mcc), atoi(mnc), strlen(mnc));

                                    hssmap = mme_hssmap_add(&plmn_id, realm, host);
                                    ogs_assert(hssmap);
                                }
                            } else
                                ogs_warn("unknown key `%s`",
                                        hss_map_key);
                        }
                    } while (ogs_yaml_iter_type(&hss_map_array) ==
                            YAML_SEQUENCE_NODE);
                } else if (!strcmp(mme_key, "security")) {
                    ogs_yaml_iter_t security_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &security_iter);
                    while (ogs_yaml_iter_next(&security_iter)) {
                        const char *security_key =
                            ogs_yaml_iter_key(&security_iter);
                        ogs_assert(security_key);
                        if (!strcmp(security_key, "integrity_order")) {
                            ogs_yaml_iter_t integrity_order_iter;
                            ogs_yaml_iter_recurse(&security_iter,
                                    &integrity_order_iter);
                            ogs_assert(ogs_yaml_iter_type(
                                        &integrity_order_iter) !=
                                YAML_MAPPING_NODE);

                            do {
                                const char *v = NULL;

                                if (ogs_yaml_iter_type(&integrity_order_iter) ==
                                        YAML_SEQUENCE_NODE) {
                                    if (!ogs_yaml_iter_next(
                                                &integrity_order_iter))
                                        break;
                                }

                                v = ogs_yaml_iter_value(&integrity_order_iter);
                                if (v) {
                                    int integrity_index =
                                        self.num_of_integrity_order;
                                    if (strcmp(v, "EIA0") == 0) {
                                        self.integrity_order[integrity_index] =
                                        OGS_NAS_SECURITY_ALGORITHMS_EIA0;
                                        self.num_of_integrity_order++;
                                    } else if (strcmp(v, "EIA1") == 0) {
                                        self.integrity_order[integrity_index] =
                                        OGS_NAS_SECURITY_ALGORITHMS_128_EIA1;
                                        self.num_of_integrity_order++;
                                    } else if (strcmp(v, "EIA2") == 0) {
                                        self.integrity_order[integrity_index] =
                                        OGS_NAS_SECURITY_ALGORITHMS_128_EIA2;
                                        self.num_of_integrity_order++;
                                    } else if (strcmp(v, "EIA3") == 0) {
                                        self.integrity_order[integrity_index] =
                                        OGS_NAS_SECURITY_ALGORITHMS_128_EIA3;
                                        self.num_of_integrity_order++;
                                    }
                                }
                            } while (
                                ogs_yaml_iter_type(&integrity_order_iter) ==
                                    YAML_SEQUENCE_NODE);
                        } else if (!strcmp(security_key, "ciphering_order")) {
                            ogs_yaml_iter_t ciphering_order_iter;
                            ogs_yaml_iter_recurse(&security_iter,
                                    &ciphering_order_iter);
                            ogs_assert(ogs_yaml_iter_type(
                                &ciphering_order_iter) != YAML_MAPPING_NODE);

                            do {
                                const char *v = NULL;

                                if (ogs_yaml_iter_type(&ciphering_order_iter) ==
                                        YAML_SEQUENCE_NODE) {
                                    if (!ogs_yaml_iter_next(
                                                &ciphering_order_iter))
                                        break;
                                }

                                v = ogs_yaml_iter_value(&ciphering_order_iter);
                                if (v) {
                                    int ciphering_index =
                                        self.num_of_ciphering_order;
                                    if (strcmp(v, "EEA0") == 0) {
                                        self.ciphering_order[ciphering_index] =
                                            OGS_NAS_SECURITY_ALGORITHMS_EEA0;
                                        self.num_of_ciphering_order++;
                                    } else if (strcmp(v, "EEA1") == 0) {
                                        self.ciphering_order[ciphering_index] =
                                        OGS_NAS_SECURITY_ALGORITHMS_128_EEA1;
                                        self.num_of_ciphering_order++;
                                    } else if (strcmp(v, "EEA2") == 0) {
                                        self.ciphering_order[ciphering_index] =
                                        OGS_NAS_SECURITY_ALGORITHMS_128_EEA2;
                                        self.num_of_ciphering_order++;
                                    } else if (strcmp(v, "EEA3") == 0) {
                                        self.ciphering_order[ciphering_index] =
                                        OGS_NAS_SECURITY_ALGORITHMS_128_EEA3;
                                        self.num_of_ciphering_order++;
                                    }
                                }
                            } while (
                                ogs_yaml_iter_type(&ciphering_order_iter) ==
                                    YAML_SEQUENCE_NODE);
                        } else
                            ogs_warn("unknown key `%s`", security_key);
                    }
                } else if (!strcmp(mme_key, "network_name")) {
                    ogs_yaml_iter_t network_name_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &network_name_iter);

                    while (ogs_yaml_iter_next(&network_name_iter)) {
                        const char *network_name_key =
                        ogs_yaml_iter_key(&network_name_iter);
                        ogs_assert(network_name_key);
                        if (!strcmp(network_name_key, "full")) {
                            ogs_nas_network_name_t *network_full_name =
                                &self.full_name;
                            const char *c_network_name =
                                ogs_yaml_iter_value(&network_name_iter);
                            uint8_t size = strlen(c_network_name);
                            uint8_t i;
                            for (i = 0;i<size;i++) {
                                /* Workaround to convert the ASCII to USC-2 */
                                network_full_name->name[i*2] = 0;
                                network_full_name->name[(i*2)+1] =
                                    c_network_name[i];

                            }
                            network_full_name->length = size*2+1;
                            network_full_name->coding_scheme = 1;
                            network_full_name->ext = 1;
                        } else if (!strcmp(network_name_key, "short")) {
                            ogs_nas_network_name_t *network_short_name =
                                &self.short_name;
                            const char *c_network_name =
                                ogs_yaml_iter_value(&network_name_iter);
                            uint8_t size = strlen(c_network_name);
                            uint8_t i;
                            for (i = 0;i<size;i++) {
                                /* Workaround to convert the ASCII to USC-2 */
                                network_short_name->name[i*2] = 0;
                                network_short_name->name[(i*2)+1] =
                                    c_network_name[i];

                            }
                            network_short_name->length = size*2+1;
                            network_short_name->coding_scheme = 1;
                            network_short_name->ext = 1;
                        } else
                            ogs_warn("unknown key `%s`", network_name_key);
                    }
                } else if (!strcmp(mme_key, "sgsap")) {
                    ogs_yaml_iter_t sgsap_array, sgsap_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &sgsap_array);
                    do {
                        mme_vlr_t *vlr = NULL;
                        ogs_plmn_id_t plmn_id;
#define MAX_NUM_OF_CSMAP            128 /* Num of TAI-LAI MAP per MME */
                        struct {
                            const char *tai_mcc, *tai_mnc;
                            const char *lai_mcc, *lai_mnc;
                            const char *tac, *lac;
                        } map[MAX_NUM_OF_CSMAP];
                        int map_num = 0;
                        ogs_sockaddr_t *addr = NULL;
                        int family = AF_UNSPEC;
                        int i, hostname_num = 0;
                        const char *hostname[OGS_MAX_NUM_OF_HOSTNAME];
                        uint16_t port = self.sgsap_port;

                        ogs_sockopt_t option;
                        bool is_option = false;

                        if (ogs_yaml_iter_type(&sgsap_array) ==
                                YAML_MAPPING_NODE) {
                            memcpy(&sgsap_iter, &sgsap_array,
                                    sizeof(ogs_yaml_iter_t));
                        } else if (ogs_yaml_iter_type(&sgsap_array) ==
                            YAML_SEQUENCE_NODE) {
                            if (!ogs_yaml_iter_next(&sgsap_array))
                                break;
                            ogs_yaml_iter_recurse(&sgsap_array, &sgsap_iter);
                        } else if (ogs_yaml_iter_type(&sgsap_array) ==
                            YAML_SCALAR_NODE) {
                            break;
                        } else
                            ogs_assert_if_reached();

                        while (ogs_yaml_iter_next(&sgsap_iter)) {
                            const char *sgsap_key =
                                ogs_yaml_iter_key(&sgsap_iter);
                            ogs_assert(sgsap_key);
                            if (!strcmp(sgsap_key, "family")) {
                                const char *v =
                                    ogs_yaml_iter_value(&sgsap_iter);
                                if (v) family = atoi(v);
                                if (family != AF_UNSPEC &&
                                    family != AF_INET && family != AF_INET6) {
                                    ogs_warn("Ignore family(%d) : "
                                        "AF_UNSPEC(%d), "
                                        "AF_INET(%d), AF_INET6(%d) ",
                                        family, AF_UNSPEC, AF_INET, AF_INET6);
                                    family = AF_UNSPEC;
                                }
                            } else if (!strcmp(sgsap_key, "addr") ||
                                    !strcmp(sgsap_key, "name")) {
                                ogs_yaml_iter_t hostname_iter;
                                ogs_yaml_iter_recurse(&sgsap_iter,
                                        &hostname_iter);
                                ogs_assert(ogs_yaml_iter_type(&hostname_iter) !=
                                    YAML_MAPPING_NODE);

                                do {
                                    if (ogs_yaml_iter_type(&hostname_iter) ==
                                            YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&hostname_iter))
                                            break;
                                    }

                                    ogs_assert(hostname_num <
                                            OGS_MAX_NUM_OF_HOSTNAME);
                                    hostname[hostname_num++] =
                                        ogs_yaml_iter_value(&hostname_iter);
                                } while (
                                    ogs_yaml_iter_type(&hostname_iter) ==
                                        YAML_SEQUENCE_NODE);
                            } else if (!strcmp(sgsap_key, "port")) {
                                const char *v =
                                    ogs_yaml_iter_value(&sgsap_iter);
                                if (v) {
                                    port = atoi(v);
                                    self.sgsap_port = port;
                                }
                            } else if (!strcmp(sgsap_key, "option")) {
                                rv = ogs_app_config_parse_sockopt(
                                        &sgsap_iter, &option);
                                if (rv != OGS_OK) return rv;
                                is_option = true;
                            } else if (!strcmp(sgsap_key, "map")) {
                                ogs_yaml_iter_t map_iter;
                                ogs_yaml_iter_recurse(&sgsap_iter, &map_iter);

                                map[map_num].tai_mcc = NULL;
                                map[map_num].tai_mnc = NULL;
                                map[map_num].tac = NULL;
                                map[map_num].lai_mcc = NULL;
                                map[map_num].lai_mnc = NULL;
                                map[map_num].lac = NULL;

                                while (ogs_yaml_iter_next(&map_iter)) {
                                    const char *map_key =
                                        ogs_yaml_iter_key(&map_iter);
                                    ogs_assert(map_key);
                                    if (!strcmp(map_key, "tai")) {
                                        ogs_yaml_iter_t tai_iter;
                                        ogs_yaml_iter_recurse(&map_iter,
                                                &tai_iter);

                                        while (ogs_yaml_iter_next(&tai_iter)) {
                                            const char *tai_key =
                                                ogs_yaml_iter_key(&tai_iter);
                                            ogs_assert(tai_key);

                                            if (!strcmp(tai_key, "plmn_id")) {
                                                ogs_yaml_iter_t plmn_id_iter;
                                                ogs_yaml_iter_recurse(&tai_iter,
                                                        &plmn_id_iter);

                                                while (ogs_yaml_iter_next(
                                                            &plmn_id_iter)) {
                                                    const char *plmn_id_key =
                                                    ogs_yaml_iter_key(
                                                            &plmn_id_iter);
                                                    ogs_assert(plmn_id_key);

                                                    if (!strcmp(plmn_id_key,
                                                                "mcc")) {
                                                        map[map_num].tai_mcc =
                                                            ogs_yaml_iter_value(
                                                                &plmn_id_iter);
                                                    } else if (!strcmp(
                                                        plmn_id_key, "mnc")) {
                                                        map[map_num].tai_mnc =
                                                            ogs_yaml_iter_value(
                                                                &plmn_id_iter);
                                                    } else
                                                        ogs_warn(
                                                            "unknown key `%s`",
                                                                plmn_id_key);
                                                }
                                            } else if (!strcmp(tai_key,
                                                        "tac")) {
                                                map[map_num].tac =
                                                    ogs_yaml_iter_value(
                                                        &tai_iter);
                                            } else
                                                ogs_warn("unknown key `%s`",
                                                        tai_key);
                                        }
                                    } else if (!strcmp(map_key, "lai")) {
                                        ogs_yaml_iter_t lai_iter;
                                        ogs_yaml_iter_recurse(&map_iter,
                                                &lai_iter);

                                        while (ogs_yaml_iter_next(&lai_iter)) {
                                            const char *lai_key =
                                                ogs_yaml_iter_key(&lai_iter);
                                            ogs_assert(lai_key);

                                            if (!strcmp(lai_key, "plmn_id")) {
                                                ogs_yaml_iter_t plmn_id_iter;
                                                ogs_yaml_iter_recurse(&lai_iter,
                                                        &plmn_id_iter);

                                                while (ogs_yaml_iter_next(
                                                            &plmn_id_iter)) {
                                                    const char *plmn_id_key =
                                                    ogs_yaml_iter_key(
                                                            &plmn_id_iter);
                                                    ogs_assert(plmn_id_key);

                                                    if (!strcmp(plmn_id_key,
                                                                "mcc")) {
                                                        map[map_num].lai_mcc =
                                                            ogs_yaml_iter_value(
                                                                &plmn_id_iter);
                                                    } else if (!strcmp(
                                                        plmn_id_key, "mnc")) {
                                                        map[map_num].lai_mnc =
                                                            ogs_yaml_iter_value(
                                                                &plmn_id_iter);
                                                    } else
                                                        ogs_warn(
                                                            "unknown key `%s`",
                                                                plmn_id_key);
                                                }
                                            } else if (!strcmp(lai_key,
                                                        "lac")) {
                                                map[map_num].lac =
                                                    ogs_yaml_iter_value(
                                                        &lai_iter);
                                            } else
                                                ogs_warn("unknown key `%s`",
                                                        lai_key);
                                        }
                                    } else
                                        ogs_warn("unknown key `%s`", map_key);
                                }

                                if (!map[map_num].tai_mcc) {
                                    ogs_error("No map.tai.plmn_id.mcc "
                                            "in configuration file");
                                    return OGS_ERROR;
                                }
                                if (!map[map_num].tai_mnc) {
                                    ogs_error("No map.tai.plmn_id.mnc "
                                            "in configuration file");
                                    return OGS_ERROR;
                                }
                                if (!map[map_num].tac) {
                                    ogs_error("No map.tai.tac "
                                            "in configuration file");
                                    return OGS_ERROR;
                                }
                                if (!map[map_num].lai_mcc) {
                                    ogs_error("No map.lai.plmn_id.mcc "
                                            "in configuration file");
                                    return OGS_ERROR;
                                }
                                if (!map[map_num].lai_mnc) {
                                    ogs_error("No map.lai.plmn_id.mnc "
                                            "in configuration file");
                                    return OGS_ERROR;
                                }
                                if (!map[map_num].lac) {
                                    ogs_error("No map.lai.lac "
                                            "in configuration file");
                                    return OGS_ERROR;
                                }

                                map_num++;

                            } else if (!strcmp(sgsap_key, "tai")) {
                                ogs_error("tai/lai configuraton changed to "
                                        "map.tai/map.lai");
                                printf("sgsap:\n"
                                    "  addr: 127.0.0.2\n"
                                    "  map:\n"
                                    "    tai:\n"
                                    "      plmn_id:\n"
                                    "        mcc: 001\n"
                                    "        mnc: 01\n"
                                    "      tac: 4131\n"
                                    "    lai:\n"
                                    "      plmn_id:\n"
                                    "        mcc: 001\n"
                                    "        mnc: 01\n"
                                    "      lac: 43691\n");
                                return OGS_ERROR;
                            } else if (!strcmp(sgsap_key, "lai")) {
                                ogs_error("tai/lai configuraton changed to "
                                        "map.tai/map.lai");
                                printf("sgsap:\n"
                                    "  addr: 127.0.0.2\n"
                                    "  map:\n"
                                    "    tai:\n"
                                    "      plmn_id:\n"
                                    "        mcc: 001\n"
                                    "        mnc: 01\n"
                                    "      tac: 4131\n"
                                    "    lai:\n"
                                    "      plmn_id:\n"
                                    "        mcc: 001\n"
                                    "        mnc: 01\n"
                                    "      lac: 43691\n");
                                return OGS_ERROR;
                            } else
                                ogs_warn("unknown key `%s`", sgsap_key);

                        }

                        if (map_num == 0) {
                            ogs_error("No TAI-LAI Map");
                            return OGS_ERROR;
                        }

                        addr = NULL;
                        for (i = 0; i < hostname_num; i++) {
                            rv = ogs_addaddrinfo(&addr,
                                    family, hostname[i], port, 0);
                            ogs_assert(rv == OGS_OK);
                        }

                        ogs_filter_ip_version(&addr,
                                ogs_app()->parameter.no_ipv4,
                                ogs_app()->parameter.no_ipv6,
                                ogs_app()->parameter.prefer_ipv4);

                        if (addr == NULL) continue;

                        vlr = mme_vlr_add(addr, is_option ? &option : NULL);
                        ogs_assert(vlr);

                        for (i = 0; i < map_num; i++) {
                            mme_csmap_t *csmap = mme_csmap_add(vlr);
                            ogs_assert(csmap);

                            ogs_plmn_id_build(&plmn_id, atoi(map[i].tai_mcc),
                                atoi(map[i].tai_mnc), strlen(map[i].tai_mnc));
                            ogs_nas_from_plmn_id(
                                    &csmap->tai.nas_plmn_id, &plmn_id);
                            csmap->tai.tac = atoi(map[i].tac);
                            ogs_plmn_id_build(&plmn_id, atoi(map[i].lai_mcc),
                                atoi(map[i].lai_mnc), strlen(map[i].lai_mnc));
                            ogs_nas_from_plmn_id(
                                    &csmap->lai.nas_plmn_id, &plmn_id);
                            csmap->lai.lac = atoi(map[i].lac);
                        }
                    } while (ogs_yaml_iter_type(&sgsap_array) ==
                            YAML_SEQUENCE_NODE);
                } else if (!strcmp(mme_key, "mme_name")) {
                    self.mme_name = ogs_yaml_iter_value(&mme_iter);
                } else if (!strcmp(mme_key, "metrics")) {
                    /* handle config in metrics library */
                } else if (!strcmp(mme_key, "eir")) {
                    ogs_yaml_iter_t eir_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &eir_iter);

                    while (ogs_yaml_iter_next(&eir_iter)) {
                        const char *eir_key = ogs_yaml_iter_key(&eir_iter);
                        ogs_assert(eir_key);
                        if (!strcmp(eir_key, "enabled")) {
                            ogs_nas_eir_t *eir = &self.eir;
                            const char *c_eir_enabled = ogs_yaml_iter_value(&eir_iter);

                            if (!strcmp("True", c_eir_enabled) || 
                                !strcmp("true", c_eir_enabled)) {
                                ogs_info("EIR functionality has been enabled");
                                eir->enabled = true;
                            }
                            else {
                                eir->enabled = false;
                            }
                        } if (!strcmp(eir_key, "allowed_states")) {
                            ogs_yaml_iter_t allowed_states_iter;
                            ogs_yaml_iter_recurse(&eir_iter,
                                    &allowed_states_iter);
                            ogs_assert(ogs_yaml_iter_type(
                                        &allowed_states_iter) !=
                                YAML_MAPPING_NODE);

                            do {
                                const char *allowed_states_value = NULL;

                                if (ogs_yaml_iter_type(&allowed_states_iter) ==
                                        YAML_SEQUENCE_NODE) {
                                    if (!ogs_yaml_iter_next(
                                                &allowed_states_iter))
                                        break;
                                }

                                allowed_states_value = ogs_yaml_iter_value(&allowed_states_iter);
                                if (allowed_states_value) {
                                    if (strcmp(allowed_states_value, "WHITELIST") == 0) {
                                        self.eir.allow_whitelist = true;
                                    } else if (strcmp(allowed_states_value, "GREYLIST") == 0) {
                                        self.eir.allow_greylist = true;
                                    } else if (strcmp(allowed_states_value, "BLACKLIST") == 0) {
                                        self.eir.allow_blacklist = true;
                                    } else {
                                        ogs_warn("'%s' is not a valid eir allowed_states value. "
                                                 "Valid values include: WHITELIST, GREYLIST, BLACKLIST",
                                                 allowed_states_value);
                                    } 
                                }
                            } while (
                                ogs_yaml_iter_type(&allowed_states_iter) ==
                                    YAML_SEQUENCE_NODE);
                        }
                    }
                } else if (!strcmp(mme_key, "emergency_number_list")) {
                    ogs_yaml_iter_t e_num_list_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &e_num_list_iter);
                    int num_emergency_number_list_items = 0;

                    /* Going through 'emergency_number_list' children */
                    while (ogs_yaml_iter_next(&e_num_list_iter)) {
                        const char *e_num_list_key = ogs_yaml_iter_key(&e_num_list_iter);
                        ogs_assert(e_num_list_key);

                        if (!strcmp(e_num_list_key, "eni")) {
                            ogs_yaml_iter_t eni_iter;
                            ogs_yaml_iter_recurse(&e_num_list_iter, &eni_iter);
                            emergency_number_list_item_t *emergency_number = &self.emergency_number_list[num_emergency_number_list_items];

                            /* Going through 'eni' children */
                            while (ogs_yaml_iter_next(&eni_iter)) {
                                const char *eni_key = ogs_yaml_iter_key(&eni_iter);
                                ogs_assert(eni_key);

                                if (!strcmp(eni_key, "services")) {
                                    ogs_yaml_iter_t services_iter;
                                    ogs_yaml_iter_recurse(&eni_iter, &services_iter);

                                    /* Going through 'services' children */
                                    while (ogs_yaml_iter_next(&services_iter)) {
                                        const char *service_value = NULL;
                                        service_value = ogs_yaml_iter_value(&services_iter);

                                        if (service_value) {
                                            if (strcmp(service_value, "MOUNTAIN_RESCUE") == 0) {
                                                emergency_number->service_mountain_rescue = true;
                                            } else if (strcmp(service_value, "MARINE_GUARD") == 0) {
                                                emergency_number->service_marine_guard = true;
                                            } else if (strcmp(service_value, "FIRE_BRIGADE") == 0) {
                                                emergency_number->service_fire_brigade = true;
                                            } else if (strcmp(service_value, "AMBULANCE") == 0) {
                                                emergency_number->service_ambulance = true;
                                            } else if (strcmp(service_value, "POLICE") == 0) {
                                                emergency_number->service_police = true;
                                            } else {
                                                ogs_warn("'%s' is not a valid emergency service. "
                                                        "Valid services include: MOUNTAIN_RESCUE, MARINE_GUARD, FIRE_BRIGADE, AMBULANCE, POLICE",
                                                        service_value);
                                            } 
                                        }
                                    }
                                }
                                else if (!strcmp(eni_key, "bcd")) {
                                    const char *bcd_decimal = ogs_yaml_iter_value(&eni_iter);
                                    if (bcd_decimal)
                                        emergency_number->bcd_decimal = atoi(bcd_decimal);
                                } 
                                else {
                                    ogs_warn("unknown key `%s`", eni_key);
                                }
                            }
                            ++num_emergency_number_list_items;
                        }
                        else {
                            ogs_warn("unknown key `%s`", e_num_list_key);
                        }
                    }
                    self.num_emergency_number_list_items = num_emergency_number_list_items;
                } else if (!strcmp(mme_key, "emergency_bearer_services")) {
                    bool *emergency_bearer_services = &self.emergency_bearer_services;
                    const char *c_emergency_bearer_services = ogs_yaml_iter_value(&mme_iter);

                    if (!strcmp("True", c_emergency_bearer_services) || 
                        !strcmp("true", c_emergency_bearer_services)) {
                        ogs_info("Emergency bearer services have been enabled");
                        *emergency_bearer_services = true;
                    }
                    else {
                        ogs_info("Emergency bearer services have been disabled");
                        *emergency_bearer_services = false;
                    }
                } else if (!strcmp(mme_key, "default_emergency_session_type")) {
                    const char *c_default_emergency_session_type = ogs_yaml_iter_value(&mme_iter);
                    self.default_emergency_session_type = atoi(c_default_emergency_session_type);
                } else if (!strcmp(mme_key, "redis_server")) {
                    ogs_yaml_iter_t redis_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &redis_iter);

                    while (ogs_yaml_iter_next(&redis_iter)) {
                    ogs_info("redis_server");
                        const char *redis_server_config_key = ogs_yaml_iter_key(&redis_iter);
                        ogs_assert(redis_server_config_key);
                        if (!strcmp(redis_server_config_key, "addr")) {
                            const char *redis_addr = ogs_yaml_iter_value(&redis_iter);
			    snprintf(self.redis_server_config.address, sizeof(self.redis_server_config.address), "%s", redis_addr);
                        } else if (!strcmp(redis_server_config_key, "port")) {
                            const char *redis_port = ogs_yaml_iter_value(&redis_iter);

                            if (redis_port)
                                self.redis_server_config.port = atoi(redis_port);
                        } else
                            ogs_warn("unknown key `%s`", mme_key);
                    }
                } else if (!strcmp(mme_key, "redis_dup_detection")) {
                    ogs_yaml_iter_t redis_dup_detection_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &redis_dup_detection_iter);

                    while (ogs_yaml_iter_next(&redis_dup_detection_iter)) {
                    ogs_info("redis_dup_detection");
                        const char *redis_dup_detection_key = ogs_yaml_iter_key(&redis_dup_detection_iter);
                        ogs_assert(redis_dup_detection_key);

                        if (!strcmp(redis_dup_detection_key, "enabled")) {
                            const char *redis_dup_detection_enabled = ogs_yaml_iter_value(&redis_dup_detection_iter);
                            if (!strcmp("True", redis_dup_detection_enabled) || 
                                !strcmp("true", redis_dup_detection_enabled)) {
                                ogs_info("Redis message duplication functionality has been enabled");
                                self.redis_dup_detection.enabled = true;
                            }
                            else {
                                self.redis_dup_detection.enabled = false;
                            }
                        } else if (!strcmp(redis_dup_detection_key, "expire_time_sec")) {
                            const char *redis_dup_detection_expire_time_sec = ogs_yaml_iter_value(&redis_dup_detection_iter);
                            if (redis_dup_detection_expire_time_sec) {
                                self.redis_dup_detection.expire_time_sec = atoi(redis_dup_detection_expire_time_sec);
                            }
                        } else
                            ogs_warn("unknown key `%s`", mme_key);
                    }
                } else if (!strcmp(mme_key, "dns")) {
                    ogs_yaml_iter_t dns_iter;
                    ogs_yaml_iter_recurse(&mme_iter, &dns_iter);

                    /* Going through keys and values in dns section */
                    while (ogs_yaml_iter_next(&dns_iter)) {
                        const char *dns_key = ogs_yaml_iter_key(&dns_iter);
                        const char *dns_value = ogs_yaml_iter_value(&dns_iter);
                        ogs_assert(dns_key);
                        ogs_assert(dns_value);

                        if (strcmp(dns_key, "dns_target_sgw") == 0) {
                            if (!strcmp("True", dns_value) || 
                                !strcmp("true", dns_value)) {
                                ogs_info("SGW DNS lookups enabled");
                                self.dns_target_sgw = true;
                            }
                        } else if (strcmp(dns_key, "dns_target_pgw") == 0) {
                            if (!strcmp("True", dns_value) || 
                                !strcmp("true", dns_value)) {
                                ogs_info("PGW DNS lookups enabled");
                                self.dns_target_pgw = true;
                            }
                        } else if (strcmp(dns_key, "base_domain") == 0) {
			    snprintf(self.dns_base_domain, MAX_DNS_BASE_DOMAIN_NAME, "%s", dns_value);
                            ogs_info("DNS lookups using base domain '%s'", self.dns_base_domain);
                        } else {
                            ogs_warn("unknown key `%s`", dns_key);
                        }
                    }
                } else if (!strcmp(mme_key, "include_local_time_zone")) {
                    const char *c_include_local_time_zone = ogs_yaml_iter_value(&mme_iter);

                    if (!strcmp("True", c_include_local_time_zone) || 
                        !strcmp("true", c_include_local_time_zone)) {
                        ogs_info("Local time IE will be included in NAS-PDU messages");
                        self.include_local_time_zone = true;
                    } else {
                        ogs_info("Local time IE will not be included in NAS-PDU messages");
                        self.include_local_time_zone = false;
                    }
                } else if (!strcmp(mme_key, "network_access_mode_default")) {
                    self.network_access_mode_default = atoi(ogs_yaml_iter_value(&mme_iter));
                } else
                    ogs_warn("unknown key `%s`", mme_key);
            }
        } else if (!strcmp(root_key, "sgwc_roaming")) {
            ogs_yaml_iter_t sgwc_roaming_iter;
            mme_sgw_t *sgw = NULL;
            ogs_sockaddr_t *addr = NULL;
            int family = AF_INET;
            int i, num = 0;
            const char *hostname[OGS_MAX_NUM_OF_HOSTNAME];
            uint16_t port = ogs_gtp_self()->gtpc_port;
            uint16_t tac[OGS_MAX_NUM_OF_TAI] = {0,};
            int num_of_tac = 0;
            uint32_t e_cell_id[OGS_MAX_NUM_OF_CELL_ID] = {0,};
            int num_of_e_cell_id = 0;

            ogs_yaml_iter_recurse(&root_iter, &sgwc_roaming_iter);
            while (ogs_yaml_iter_next(&sgwc_roaming_iter)) {
                const char *sgwc_roaming_key = ogs_yaml_iter_key(&sgwc_roaming_iter);
                ogs_assert(sgwc_roaming_key);
                if (!strcmp(sgwc_roaming_key, "gtpc")) {
                    ogs_yaml_iter_t gtpc_array, gtpc_iter;
                    ogs_yaml_iter_recurse(&sgwc_roaming_iter, &gtpc_array);
                    do {
                        if (ogs_yaml_iter_type(&gtpc_array) ==
                                YAML_MAPPING_NODE) {
                            memcpy(&gtpc_iter, &gtpc_array,
                                    sizeof(ogs_yaml_iter_t));
                        } else if (ogs_yaml_iter_type(&gtpc_array) ==
                            YAML_SEQUENCE_NODE) {
                            if (!ogs_yaml_iter_next(&gtpc_array))
                                break;
                            ogs_yaml_iter_recurse(&gtpc_array, &gtpc_iter);
                        } else if (ogs_yaml_iter_type(&gtpc_array) ==
                                YAML_SCALAR_NODE) {
                            break;
                        } else
                            ogs_assert_if_reached();

                        while (ogs_yaml_iter_next(&gtpc_iter)) {
                            const char *gtpc_key =
                                ogs_yaml_iter_key(&gtpc_iter);
                            ogs_assert(gtpc_key);
                            if (!strcmp(gtpc_key, "addr")) {
                                ogs_yaml_iter_t hostname_iter;
                                ogs_yaml_iter_recurse(&gtpc_iter,
                                        &hostname_iter);
                                ogs_assert(ogs_yaml_iter_type(&hostname_iter) !=
                                    YAML_MAPPING_NODE);

                                do {
                                    if (ogs_yaml_iter_type(&hostname_iter) ==
                                            YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&hostname_iter))
                                            break;
                                    }

                                    ogs_assert(num < OGS_MAX_NUM_OF_HOSTNAME);
                                    hostname[num++] =
                                        ogs_yaml_iter_value(&hostname_iter);
                                } while (
                                    ogs_yaml_iter_type(&hostname_iter) ==
                                        YAML_SEQUENCE_NODE);
                            } else
                                ogs_warn("unknown key `%s`", gtpc_key);
                        }
                    } while (ogs_yaml_iter_type(&gtpc_array) ==
                            YAML_SEQUENCE_NODE);
                }
            }

            addr = NULL;
            for (i = 0; i < num; i++) {
                rv = ogs_addaddrinfo(&addr,
                        family, hostname[i], port, 0);
                ogs_assert(rv == OGS_OK);
            }

            while (addr) {
                ogs_filter_ip_version(&addr,
                        ogs_app()->parameter.no_ipv4,
                        ogs_app()->parameter.no_ipv6,
                        ogs_app()->parameter.prefer_ipv4);

                if (addr == NULL) continue;

                sgw = mme_sgw_roaming_add(addr);
                ogs_assert(sgw);

                sgw->num_of_tac = num_of_tac;
                if (num_of_tac != 0)
                    memcpy(sgw->tac, tac, sizeof(sgw->tac));

                sgw->num_of_e_cell_id = num_of_e_cell_id;
                if (num_of_e_cell_id != 0)
                    memcpy(sgw->e_cell_id, e_cell_id,
                            sizeof(sgw->e_cell_id));

                addr = addr->next;
            }
        } else if (!strcmp(root_key, "sgw") || !strcmp(root_key, "sgwc")) {
            ogs_yaml_iter_t sgw_iter;
            ogs_yaml_iter_recurse(&root_iter, &sgw_iter);
            while (ogs_yaml_iter_next(&sgw_iter)) {
                const char *sgw_key = ogs_yaml_iter_key(&sgw_iter);
                ogs_assert(sgw_key);
                if (!strcmp(sgw_key, "gtpc")) {
                    ogs_yaml_iter_t gtpc_array, gtpc_iter;
                    ogs_yaml_iter_recurse(&sgw_iter, &gtpc_array);
                    do {
                        mme_sgw_t *sgw = NULL;
                        ogs_sockaddr_t *addr = NULL;
                        int family = AF_UNSPEC;
                        int i, num = 0;
                        const char *hostname[OGS_MAX_NUM_OF_HOSTNAME];
                        uint16_t port = ogs_gtp_self()->gtpc_port;
                        uint16_t tac[OGS_MAX_NUM_OF_TAI] = {0,};
                        int num_of_tac = 0;
                        uint32_t e_cell_id[OGS_MAX_NUM_OF_CELL_ID] = {0,};
                        int num_of_e_cell_id = 0;

                        if (ogs_yaml_iter_type(&gtpc_array) ==
                                YAML_MAPPING_NODE) {
                            memcpy(&gtpc_iter, &gtpc_array,
                                    sizeof(ogs_yaml_iter_t));
                        } else if (ogs_yaml_iter_type(&gtpc_array) ==
                            YAML_SEQUENCE_NODE) {
                            if (!ogs_yaml_iter_next(&gtpc_array))
                                break;
                            ogs_yaml_iter_recurse(&gtpc_array, &gtpc_iter);
                        } else if (ogs_yaml_iter_type(&gtpc_array) ==
                                YAML_SCALAR_NODE) {
                            break;
                        } else
                            ogs_assert_if_reached();

                        while (ogs_yaml_iter_next(&gtpc_iter)) {
                            const char *gtpc_key =
                                ogs_yaml_iter_key(&gtpc_iter);
                            ogs_assert(gtpc_key);
                            if (!strcmp(gtpc_key, "family")) {
                                const char *v = ogs_yaml_iter_value(&gtpc_iter);
                                if (v) family = atoi(v);
                                if (family != AF_UNSPEC &&
                                    family != AF_INET && family != AF_INET6) {
                                    ogs_warn("Ignore family(%d) : "
                                        "AF_UNSPEC(%d), "
                                        "AF_INET(%d), AF_INET6(%d) ",
                                        family, AF_UNSPEC, AF_INET, AF_INET6);
                                    family = AF_UNSPEC;
                                }
                            } else if (!strcmp(gtpc_key, "addr") ||
                                    !strcmp(gtpc_key, "name")) {
                                ogs_yaml_iter_t hostname_iter;
                                ogs_yaml_iter_recurse(&gtpc_iter,
                                        &hostname_iter);
                                ogs_assert(ogs_yaml_iter_type(&hostname_iter) !=
                                    YAML_MAPPING_NODE);

                                do {
                                    if (ogs_yaml_iter_type(&hostname_iter) ==
                                            YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&hostname_iter))
                                            break;
                                    }

                                    ogs_assert(num < OGS_MAX_NUM_OF_HOSTNAME);
                                    hostname[num++] =
                                        ogs_yaml_iter_value(&hostname_iter);
                                } while (
                                    ogs_yaml_iter_type(&hostname_iter) ==
                                        YAML_SEQUENCE_NODE);
                            } else if (!strcmp(gtpc_key, "port")) {
                                const char *v = ogs_yaml_iter_value(&gtpc_iter);
                                if (v) port = atoi(v);
                            } else if (!strcmp(gtpc_key, "tac")) {
                                ogs_yaml_iter_t tac_iter;
                                ogs_yaml_iter_recurse(&gtpc_iter, &tac_iter);
                                ogs_assert(ogs_yaml_iter_type(&tac_iter) !=
                                    YAML_MAPPING_NODE);

                                do {
                                    const char *v = NULL;

                                    ogs_assert(num_of_tac <
                                            OGS_MAX_NUM_OF_TAI);
                                    if (ogs_yaml_iter_type(&tac_iter) ==
                                            YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&tac_iter))
                                            break;
                                    }

                                    v = ogs_yaml_iter_value(&tac_iter);
                                    if (v) {
                                        tac[num_of_tac] = atoi(v);
                                        num_of_tac++;
                                    }
                                } while (
                                    ogs_yaml_iter_type(&tac_iter) ==
                                        YAML_SEQUENCE_NODE);
                            } else if (!strcmp(gtpc_key, "e_cell_id")) {
                                ogs_yaml_iter_t e_cell_id_iter;
                                ogs_yaml_iter_recurse(&gtpc_iter,
                                        &e_cell_id_iter);
                                ogs_assert(ogs_yaml_iter_type(&e_cell_id_iter)
                                        != YAML_MAPPING_NODE);

                                do {
                                    const char *v = NULL;

                                    ogs_assert(num_of_e_cell_id <
                                            OGS_MAX_NUM_OF_CELL_ID);
                                    if (ogs_yaml_iter_type(&e_cell_id_iter) ==
                                            YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(
                                                    &e_cell_id_iter))
                                            break;
                                    }
                                    v = ogs_yaml_iter_value(&e_cell_id_iter);
                                    if (v) {
                                        e_cell_id[num_of_e_cell_id]
                                            = ogs_uint64_from_string((char*)v);
                                        num_of_e_cell_id++;
                                    }
                                } while (
                                    ogs_yaml_iter_type(&e_cell_id_iter) ==
                                        YAML_SEQUENCE_NODE);
                            } else
                                ogs_warn("unknown key `%s`", gtpc_key);
                        }

                        addr = NULL;
                        for (i = 0; i < num; i++) {
                            rv = ogs_addaddrinfo(&addr,
                                    family, hostname[i], port, 0);
                            ogs_assert(rv == OGS_OK);
                        }

                        while (addr) {
                            ogs_filter_ip_version(&addr,
                                    ogs_app()->parameter.no_ipv4,
                                    ogs_app()->parameter.no_ipv6,
                                    ogs_app()->parameter.prefer_ipv4);

                            if (addr == NULL) continue;

                            sgw = mme_sgw_add(addr);
                            ogs_assert(sgw);

                            sgw->num_of_tac = num_of_tac;
                            if (num_of_tac != 0)
                                memcpy(sgw->tac, tac, sizeof(sgw->tac));

                            sgw->num_of_e_cell_id = num_of_e_cell_id;
                            if (num_of_e_cell_id != 0)
                                memcpy(sgw->e_cell_id, e_cell_id,
                                        sizeof(sgw->e_cell_id));

                            addr = addr->next;
                        }
                    } while (ogs_yaml_iter_type(&gtpc_array) ==
                            YAML_SEQUENCE_NODE);
                }
            }
        } else if (!strcmp(root_key, "pgw") || !strcmp(root_key, "smf")) {
            ogs_yaml_iter_t pgw_iter;
            ogs_yaml_iter_recurse(&root_iter, &pgw_iter);
            while (ogs_yaml_iter_next(&pgw_iter)) {
                const char *pgw_key = ogs_yaml_iter_key(&pgw_iter);
                ogs_assert(pgw_key);
                if (!strcmp(pgw_key, "gtpc")) {
                    ogs_yaml_iter_t gtpc_array, gtpc_iter;
                    ogs_yaml_iter_recurse(&pgw_iter, &gtpc_array);
                    do {
                        mme_pgw_t *pgw = NULL;
                        ogs_sockaddr_t *addr = NULL;
                        int family = AF_UNSPEC;
                        int i, num = 0;
                        const char *hostname[OGS_MAX_NUM_OF_HOSTNAME];
                        const char *apn = NULL;
                        uint16_t port = ogs_gtp_self()->gtpc_port;

                        if (ogs_yaml_iter_type(&gtpc_array) ==
                                YAML_MAPPING_NODE) {
                            memcpy(&gtpc_iter, &gtpc_array,
                                    sizeof(ogs_yaml_iter_t));
                        } else if (ogs_yaml_iter_type(&gtpc_array) ==
                            YAML_SEQUENCE_NODE) {
                            if (!ogs_yaml_iter_next(&gtpc_array))
                                break;
                            ogs_yaml_iter_recurse(&gtpc_array, &gtpc_iter);
                        } else if (ogs_yaml_iter_type(&gtpc_array) ==
                                YAML_SCALAR_NODE) {
                            break;
                        } else
                            ogs_assert_if_reached();

                        while (ogs_yaml_iter_next(&gtpc_iter)) {
                            const char *gtpc_key =
                                ogs_yaml_iter_key(&gtpc_iter);
                            ogs_assert(gtpc_key);
                            if (!strcmp(gtpc_key, "family")) {
                                const char *v = ogs_yaml_iter_value(&gtpc_iter);
                                if (v) family = atoi(v);
                                if (family != AF_UNSPEC &&
                                    family != AF_INET && family != AF_INET6) {
                                    ogs_warn("Ignore family(%d) : "
                                        "AF_UNSPEC(%d), "
                                        "AF_INET(%d), AF_INET6(%d) ",
                                        family, AF_UNSPEC, AF_INET, AF_INET6);
                                    family = AF_UNSPEC;
                                }
                            } else if (!strcmp(gtpc_key, "addr") ||
                                    !strcmp(gtpc_key, "name")) {
                                ogs_yaml_iter_t hostname_iter;
                                ogs_yaml_iter_recurse(
                                        &gtpc_iter, &hostname_iter);
                                ogs_assert(ogs_yaml_iter_type(&hostname_iter) !=
                                    YAML_MAPPING_NODE);

                                do {
                                    if (ogs_yaml_iter_type(&hostname_iter) ==
                                            YAML_SEQUENCE_NODE) {
                                        if (!ogs_yaml_iter_next(&hostname_iter))
                                            break;
                                    }

                                    ogs_assert(num < OGS_MAX_NUM_OF_HOSTNAME);
                                    hostname[num++] =
                                        ogs_yaml_iter_value(&hostname_iter);
                                } while (
                                    ogs_yaml_iter_type(&hostname_iter) ==
                                        YAML_SEQUENCE_NODE);
                            } else if (!strcmp(gtpc_key, "port")) {
                                const char *v = ogs_yaml_iter_value(&gtpc_iter);
                                if (v) port = atoi(v);
                            } else if (!strcmp(gtpc_key, "apn")) {
                                apn = ogs_yaml_iter_value(&gtpc_iter);
                            } else
                                ogs_warn("unknown key `%s`", gtpc_key);
                        }

                        addr = NULL;
                        for (i = 0; i < num; i++) {
                            rv = ogs_addaddrinfo(&addr,
                                    family, hostname[i], port, 0);
                            ogs_assert(rv == OGS_OK);
                        }

                        ogs_filter_ip_version(&addr,
                                ogs_app()->parameter.no_ipv4,
                                ogs_app()->parameter.no_ipv6,
                                ogs_app()->parameter.prefer_ipv4);

                        if (addr == NULL) continue;

                        pgw = mme_pgw_add(addr);
                        ogs_assert(pgw);

                        pgw->apn = apn;

                    } while (ogs_yaml_iter_type(&gtpc_array) ==
                            YAML_SEQUENCE_NODE);
                }
            }
        } else if (!strcmp(root_key, "time")) {
            ogs_yaml_iter_t time_iter;
            ogs_yaml_iter_recurse(&root_iter, &time_iter);
            while (ogs_yaml_iter_next(&time_iter)) {
                const char *time_key = ogs_yaml_iter_key(&time_iter);
                ogs_assert(time_key);
                if (!strcmp(time_key, "t3402")) {
                    ogs_yaml_iter_t t3402_iter;
                    ogs_yaml_iter_recurse(&time_iter, &t3402_iter);

                    while (ogs_yaml_iter_next(&t3402_iter)) {
                        const char *t3402_key =
                            ogs_yaml_iter_key(&t3402_iter);
                        ogs_assert(t3402_key);

                        if (!strcmp(t3402_key, "value")) {
                            const char *v = ogs_yaml_iter_value(&t3402_iter);
                            if (v)
                                self.time.t3402.value = atoll(v);
                        } else
                            ogs_warn("unknown key `%s`", t3402_key);
                    }
                } else if (!strcmp(time_key, "t3412")) {
                    ogs_yaml_iter_t t3412_iter;
                    ogs_yaml_iter_recurse(&time_iter, &t3412_iter);

                    while (ogs_yaml_iter_next(&t3412_iter)) {
                        const char *t3412_key =
                            ogs_yaml_iter_key(&t3412_iter);
                        ogs_assert(t3412_key);

                        if (!strcmp(t3412_key, "value")) {
                            const char *v = ogs_yaml_iter_value(&t3412_iter);
                            if (v)
                                self.time.t3412.value = atoll(v);
                        } else
                            ogs_warn("unknown key `%s`", t3412_key);
                    }
                } else if (!strcmp(time_key, "t3423")) {
                    ogs_yaml_iter_t t3423_iter;
                    ogs_yaml_iter_recurse(&time_iter, &t3423_iter);

                    while (ogs_yaml_iter_next(&t3423_iter)) {
                        const char *t3423_key =
                            ogs_yaml_iter_key(&t3423_iter);
                        ogs_assert(t3423_key);

                        if (!strcmp(t3423_key, "value")) {
                            const char *v = ogs_yaml_iter_value(&t3423_iter);
                            if (v)
                                self.time.t3423.value = atoll(v);
                        } else
                            ogs_warn("unknown key `%s`", t3423_key);
                    }
                } else if (!strcmp(time_key, "t3512")) {
                    /* handle config in amf */
                } else if (!strcmp(time_key, "nf_instance")) {
                    /* handle config in app library */
                } else if (!strcmp(time_key, "subscription")) {
                    /* handle config in app library */
                } else if (!strcmp(time_key, "message")) {
                    /* handle config in app library */
                } else if (!strcmp(time_key, "handover")) {
                    /* handle config in app library */
                } else
                    ogs_warn("unknown key `%s`", time_key);
            }
        }
    }

    rv = mme_context_validation();
    if (rv != OGS_OK) return rv;

    return OGS_OK;
}

mme_sgw_t *mme_sgw_add(ogs_sockaddr_t *addr)
{
    mme_sgw_t *sgw = NULL;

    ogs_assert(addr);

    ogs_pool_alloc(&mme_sgw_pool, &sgw);
    ogs_assert(sgw);
    memset(sgw, 0, sizeof *sgw);

    sgw->gnode.sa_list = addr;

    ogs_list_init(&sgw->gnode.local_list);
    ogs_list_init(&sgw->gnode.remote_list);

    ogs_list_init(&sgw->sgw_ue_list);

    ogs_list_add(&self.sgw_list, sgw);

    return sgw;
}

void mme_sgw_remove(mme_sgw_t *sgw)
{
    ogs_assert(sgw);

    ogs_list_remove(&self.sgw_list, sgw);

    ogs_gtp_xact_delete_all(&sgw->gnode);
    ogs_freeaddrinfo(sgw->gnode.sa_list);

    /* Clear sgw so if pointer is used again use-after-free is easier to detect */
    memset(sgw, 0, sizeof(*sgw));

    ogs_pool_free(&mme_sgw_pool, sgw);
}

void mme_sgw_remove_all(void)
{
    mme_sgw_t *sgw = NULL, *next_sgw = NULL;

    ogs_list_for_each_safe(&self.sgw_list, next_sgw, sgw)
        mme_sgw_remove(sgw);
}

mme_sgw_t *mme_sgw_find_by_addr(ogs_sockaddr_t *addr)
{
    mme_sgw_t *sgw = NULL;

    ogs_assert(addr);

    ogs_list_for_each(&self.sgw_list, sgw) {
        if (ogs_sockaddr_is_equal(&sgw->gnode.addr, addr) == true)
            break;
    }

    return sgw;
}

mme_sgw_t *mme_sgw_roaming_add(ogs_sockaddr_t *addr)
{
    mme_sgw_t *sgw = NULL;

    ogs_assert(addr);

    ogs_pool_alloc(&mme_sgw_pool, &sgw);
    ogs_assert(sgw);
    memset(sgw, 0, sizeof *sgw);

    sgw->gnode.sa_list = addr;

    ogs_list_init(&sgw->gnode.local_list);
    ogs_list_init(&sgw->gnode.remote_list);

    ogs_list_init(&sgw->sgw_ue_list);

    ogs_list_add(&self.sgw_roaming_list, sgw);

    return sgw;
}

void mme_sgw_roaming_remove(mme_sgw_t *sgw)
{
    ogs_assert(sgw);

    ogs_list_remove(&self.sgw_roaming_list, sgw);

    ogs_gtp_xact_delete_all(&sgw->gnode);
    ogs_freeaddrinfo(sgw->gnode.sa_list);

    /* Clear sgw so if pointer is used again use-after-free is easier to detect */
    memset(sgw, 0, sizeof(*sgw));

    ogs_pool_free(&mme_sgw_pool, sgw);
}

void mme_sgw_roaming_remove_all(void)
{
    mme_sgw_t *sgw = NULL, *next_sgw = NULL;

    ogs_list_for_each_safe(&self.sgw_roaming_list, next_sgw, sgw)
        mme_sgw_roaming_remove(sgw);
}

mme_sgw_t *mme_sgw_roaming_find_by_addr(ogs_sockaddr_t *addr)
{
    mme_sgw_t *sgw = NULL;

    ogs_assert(addr);

    ogs_list_for_each(&self.sgw_roaming_list, sgw) {
        if (ogs_sockaddr_is_equal(&sgw->gnode.addr, addr) == true)
            break;
    }

    return sgw;
}

mme_sgw_t *select_random_sgw()
{
    char buf[OGS_ADDRSTRLEN];
    mme_sgw_t *random;
    int sgw_count;
    int index;

    /* Select a random sgw */
    sgw_count = ogs_list_count(&mme_self()->sgw_list);
    index = rand_under(sgw_count);
    ogs_debug("There are %i SGWs in our list, we have randomly picked the one at index %i", sgw_count, index);
    random = ogs_list_at(&mme_self()->sgw_list, index);

    ogs_info(
        "SGWC address chosen was '%s'",
        OGS_ADDR(random->gnode.sa_list, buf)
    );

    return random;
}

mme_sgw_t *select_random_sgw_roaming()
{
    char buf[OGS_ADDRSTRLEN];
    mme_sgw_t *random;
    int sgw_count;
    int index;

    /* Select a random sgw */
    sgw_count = ogs_list_count(&mme_self()->sgw_roaming_list);

    if (0 == sgw_count) {
        ogs_error("There are no roaming SGWs in our list");
        return NULL;
    }

    index = rand_under(sgw_count);
    ogs_info("There are %i roaming SGWs in our list, we have randomly picked the one at index %i", sgw_count, index);
    random = ogs_list_at(&mme_self()->sgw_roaming_list, index);

    ogs_info(
        "Roaming SGWC address chosen was '%s'",
        OGS_ADDR(random->gnode.sa_list, buf)
    );

    return random;
}

mme_pgw_t *mme_pgw_add(ogs_sockaddr_t *addr)
{
    mme_pgw_t *pgw = NULL;

    ogs_assert(addr);

    ogs_pool_alloc(&mme_pgw_pool, &pgw);
    ogs_assert(pgw);
    memset(pgw, 0, sizeof *pgw);

    pgw->sa_list = addr;

    ogs_list_add(&self.pgw_list, pgw);

    return pgw;
}

void mme_pgw_remove(mme_pgw_t *pgw)
{
    ogs_assert(pgw);

    ogs_list_remove(&self.pgw_list, pgw);

    ogs_freeaddrinfo(pgw->sa_list);

    /* Clear pgw so if pointer is used again use-after-free is easier to detect */
    memset(pgw, 0, sizeof(*pgw));

    ogs_pool_free(&mme_pgw_pool, pgw);
}

void mme_pgw_remove_all(void)
{
    mme_pgw_t *pgw = NULL, *next_pgw = NULL;

    ogs_list_for_each_safe(&self.pgw_list, next_pgw, pgw)
        mme_pgw_remove(pgw);
}

ogs_sockaddr_t *mme_pgw_addr_select_random(ogs_list_t *list, int family)
{
    enum {ADDR_BUF_SZ = 32};
    ogs_sockaddr_t *addr_buf[ADDR_BUF_SZ] = {0};
    ogs_sockaddr_t *addr = NULL;
    mme_pgw_t *pgw = NULL;
    char buf[OGS_ADDRSTRLEN];
    int index = 0;
    int addr_count = 0;
    
    ogs_assert(list);

    /* Get all addresses for this family */
    ogs_list_for_each(list, pgw) {
        ogs_assert(pgw->sa_list);
        ogs_sockaddr_t *addr = pgw->sa_list;

        while (addr && (addr_count < ADDR_BUF_SZ)) {
            if (addr->ogs_sa_family == family) {
                addr_buf[addr_count] = addr;
                ++addr_count;
            }
            addr = addr->next;
        }
    }

    ogs_debug("There are %i/%i PGW addresses we can pick from given the family type of %i", addr_count, ADDR_BUF_SZ, family);
    
    if (0 == addr_count) {
        ogs_info("No viable PGW addresses for family %i, returning NULL", family);
        return NULL;
    }

    index = rand_under(addr_count);
    ogs_debug("We have randomly picked the PGW address at index %i", index);
    ogs_assert(index < addr_count);
    addr = addr_buf[index];

    ogs_info(
        "PWG address chosen was '%s' (for family %i)",
        OGS_ADDR(addr, buf),
        family
    );

    return addr;
}

ogs_sockaddr_t *mme_pgw_addr_find_by_apn(
        ogs_list_t *list, int family, char *apn)
{
    mme_pgw_t *pgw = NULL;
    ogs_assert(list);

    ogs_list_for_each(list, pgw) {
        ogs_assert(pgw->sa_list);
        ogs_sockaddr_t *addr = pgw->sa_list;

        while (addr) {
            if (addr->ogs_sa_family == family &&
                (!apn || (pgw->apn && !ogs_strcasecmp(apn, pgw->apn)))) {
                return addr;
            }
            addr = addr->next;
        }
    }

    return NULL;
}

mme_vlr_t *mme_vlr_add(ogs_sockaddr_t *sa_list, ogs_sockopt_t *option)
{
    mme_vlr_t *vlr = NULL;

    ogs_assert(sa_list);

    ogs_pool_alloc(&mme_vlr_pool, &vlr);
    ogs_assert(vlr);
    memset(vlr, 0, sizeof *vlr);

    vlr->max_num_of_ostreams = OGS_DEFAULT_SCTP_MAX_NUM_OF_OSTREAMS;
    vlr->ostream_id = 0;

    vlr->sa_list = sa_list;
    if (option) {
        vlr->max_num_of_ostreams = option->sctp.sinit_num_ostreams;
        vlr->option = ogs_memdup(option, sizeof *option);
    }

    ogs_list_add(&self.vlr_list, vlr);

    return vlr;
}

void mme_vlr_remove(mme_vlr_t *vlr)
{
    ogs_assert(vlr);

    ogs_list_remove(&self.vlr_list, vlr);

    mme_vlr_close(vlr);

    ogs_freeaddrinfo(vlr->sa_list);
    if (vlr->option)
        ogs_free(vlr->option);

    /* Clear vlr so if pointer is used again use-after-free is easier to detect */
    memset(vlr, 0, sizeof(*vlr));

    ogs_pool_free(&mme_vlr_pool, vlr);
}

void mme_vlr_remove_all(void)
{
    mme_vlr_t *vlr = NULL, *next_vlr = NULL;

    ogs_list_for_each_safe(&self.vlr_list, next_vlr, vlr)
        mme_vlr_remove(vlr);
}

void mme_vlr_close(mme_vlr_t *vlr)
{
    ogs_assert(vlr);

    if (vlr->poll)
        ogs_pollset_remove(vlr->poll);
    if (vlr->sock)
        ogs_sctp_destroy(vlr->sock);
}

mme_vlr_t *mme_vlr_find_by_addr(ogs_sockaddr_t *addr)
{
    mme_vlr_t *vlr = NULL;
    ogs_assert(addr);

    ogs_list_for_each(&self.vlr_list, vlr) {
        if (ogs_sockaddr_is_equal(vlr->addr, addr) == true)
            return vlr;
    }

    return NULL;
}

mme_csmap_t *mme_csmap_add(mme_vlr_t *vlr)
{
    mme_csmap_t *csmap = NULL;

    ogs_assert(vlr);

    ogs_pool_alloc(&mme_csmap_pool, &csmap);
    ogs_assert(csmap);
    memset(csmap, 0, sizeof *csmap);

    csmap->vlr = vlr;

    ogs_list_add(&self.csmap_list, csmap);

    return csmap;
}

void mme_csmap_remove(mme_csmap_t *csmap)
{
    ogs_assert(csmap);

    ogs_list_remove(&self.csmap_list, csmap);

    /* Clear csmap so if pointer is used again use-after-free is easier to detect */
    memset(csmap, 0, sizeof(*csmap));

    ogs_pool_free(&mme_csmap_pool, csmap);
}

void mme_csmap_remove_all(void)
{
    mme_csmap_t *csmap = NULL, *next_csmap = NULL;

    ogs_list_for_each_safe(&self.csmap_list, next_csmap, csmap)
        mme_csmap_remove(csmap);
}

mme_csmap_t *mme_csmap_find_by_tai(ogs_eps_tai_t *tai)
{
    mme_csmap_t *csmap = NULL;
    ogs_assert(tai);

    ogs_list_for_each(&self.csmap_list, csmap) {
        ogs_nas_eps_tai_t ogs_nas_tai;
        ogs_nas_from_plmn_id(&ogs_nas_tai.nas_plmn_id, &tai->plmn_id);
        ogs_nas_tai.tac = tai->tac;
        if (memcmp(&csmap->tai, &ogs_nas_tai, sizeof(ogs_nas_eps_tai_t)) == 0)
            return csmap;
    }

    return NULL;
}

mme_csmap_t *mme_csmap_find_by_nas_lai(ogs_nas_lai_t *lai)
{
    mme_csmap_t *csmap = NULL;
    ogs_assert(lai);

    ogs_list_for_each(&self.csmap_list, csmap) {
        if (memcmp(&csmap->lai, lai, sizeof *lai) == 0)
            return csmap;
    }

    return NULL;
}

mme_hssmap_t *mme_hssmap_add(ogs_plmn_id_t *plmn_id, const char *realm,
                             const char *host)
{
    mme_hssmap_t *hssmap = NULL;

    ogs_assert(plmn_id);

    ogs_pool_alloc(&mme_hssmap_pool, &hssmap);
    ogs_assert(hssmap);
    memset(hssmap, 0, sizeof *hssmap);

    hssmap->plmn_id = *plmn_id;
    if (realm)
        hssmap->realm = ogs_strdup(realm);
    else
        hssmap->realm = ogs_epc_domain_from_plmn_id(plmn_id);

    if (host)
        hssmap->host = ogs_strdup(host);
    else
        hssmap->host = NULL;

    ogs_list_add(&self.hssmap_list, hssmap);

    return hssmap;
}

void mme_hssmap_remove(mme_hssmap_t *hssmap)
{
    ogs_assert(hssmap);

    ogs_list_remove(&self.hssmap_list, hssmap);

    if (hssmap->realm != NULL)
        ogs_free(hssmap->realm);

    if (hssmap->host != NULL)
        ogs_free(hssmap->host);

    ogs_pool_free(&mme_hssmap_pool, hssmap);
}

void mme_hssmap_remove_all(void)
{
    mme_hssmap_t *hssmap = NULL, *next_hssmap = NULL;

    ogs_list_for_each_safe(&self.hssmap_list, next_hssmap, hssmap)
        mme_hssmap_remove(hssmap);
}

mme_hssmap_t *mme_hssmap_find_by_imsi_bcd(const char *imsi_bcd)
{
    mme_hssmap_t *hssmap = NULL;
    ogs_assert(imsi_bcd);

    ogs_list_for_each(&self.hssmap_list, hssmap) {
        char plmn_id_str[OGS_PLMNIDSTRLEN] = "";

        ogs_plmn_id_to_string(&hssmap->plmn_id, plmn_id_str);
        if (strncmp(plmn_id_str, imsi_bcd, strlen(plmn_id_str)) == 0) {
            return hssmap;
        }
    }

    return NULL;
}

mme_enb_t *mme_enb_add(ogs_sock_t *sock, ogs_sockaddr_t *addr)
{
    mme_enb_t *enb = NULL;
    mme_event_t e;

    ogs_assert(sock);
    ogs_assert(addr);

    ogs_pool_alloc(&mme_enb_pool, &enb);
    ogs_assert(enb);
    memset(enb, 0, sizeof *enb);

    enb->sctp.sock = sock;
    enb->sctp.addr = addr;
    enb->sctp.type = mme_enb_sock_type(enb->sctp.sock);

    if (enb->sctp.type == SOCK_STREAM) {
        enb->sctp.poll.read = ogs_pollset_add(ogs_app()->pollset,
            OGS_POLLIN, sock->fd, s1ap_recv_upcall, sock);
        ogs_assert(enb->sctp.poll.read);

        ogs_list_init(&enb->sctp.write_queue);
    }

    enb->max_num_of_ostreams = 0;
    enb->ostream_id = 0;

    ogs_list_init(&enb->enb_ue_list);

    ogs_hash_set(self.enb_addr_hash,
            enb->sctp.addr, sizeof(ogs_sockaddr_t), enb);

    memset(&e, 0, sizeof(e));
    e.enb = enb;
    ogs_fsm_init(&enb->sm, s1ap_state_initial, s1ap_state_final, &e);

    ogs_list_add(&self.enb_list, enb);

    char buf[OGS_ADDRSTRLEN];
    OGS_ADDR(addr, buf);
    mme_metrics_connected_enb_add(buf);

    ogs_info("[Added] Number of eNBs is now %d",
            ogs_list_count(&self.enb_list));

    return enb;
}

int mme_enb_remove(mme_enb_t *enb)
{
    mme_event_t e;

    ogs_assert(enb);
    ogs_assert(enb->sctp.sock);

    ogs_list_remove(&self.enb_list, enb);

    memset(&e, 0, sizeof(e));
    e.enb = enb;
    ogs_fsm_fini(&enb->sm, &e);

    ogs_hash_set(self.enb_addr_hash,
            enb->sctp.addr, sizeof(ogs_sockaddr_t), NULL);
    ogs_hash_set(self.enb_id_hash, &enb->enb_id, sizeof(enb->enb_id), NULL);

    /*
     * CHECK:
     *
     * S1-Reset Ack buffer is not cleared at this point.
     * ogs_sctp_flush_and_destroy will clear this buffer
     */
    char buf[OGS_ADDRSTRLEN];
    OGS_ADDR(enb->sctp.addr, buf);
    mme_metrics_connected_enb_clear(buf);
    char cell_id[16] = ""; // todo give this a real number
    sprintf(cell_id, "%u", enb->enb_id);
    mme_metrics_connected_enb_id_clear(buf, cell_id);

    ogs_sctp_flush_and_destroy(&enb->sctp);

    ogs_pool_free(&mme_enb_pool, enb);
    ogs_info("[Removed] Number of eNBs is now %d",
            ogs_list_count(&self.enb_list));

    return OGS_OK;
}

int mme_enb_remove_all(void)
{
    mme_enb_t *enb = NULL, *next_enb = NULL;

    ogs_list_for_each_safe(&self.enb_list, next_enb, enb)
        mme_enb_remove(enb);

    return OGS_OK;
}

bool mme_cbc_initialised(void)
{
    return self.cbc.state.initialised;
}

int mme_cbc_init(ogs_sock_t *sock, ogs_sockaddr_t *addr)
{
    mme_cbc_t * cbc = &self.cbc;

    cbc->sctp.sock = sock;
    cbc->sctp.addr = addr;
    cbc->sctp.type = SOCK_STREAM; /* TODO: Add other option like mme_enb_sock_type */

    if (cbc->sctp.type == SOCK_STREAM) {
        cbc->sctp.poll.read = ogs_pollset_add(ogs_app()->pollset,
            OGS_POLLIN, sock->fd, sbcap_recv_upcall, sock);
        ogs_assert(cbc->sctp.poll.read);

        ogs_list_init(&cbc->sctp.write_queue);
    }

    cbc->state.initialised = true;

    return OGS_OK;
}

int mme_cbc_remove(void)
{
    ogs_sctp_flush_and_destroy(&self.cbc.sctp);

    memset(&self.cbc, 0, sizeof(self.cbc));

    return OGS_OK;
}

mme_enb_t *mme_enb_find_by_addr(ogs_sockaddr_t *addr)
{
    ogs_assert(addr);
    return (mme_enb_t *)ogs_hash_get(self.enb_addr_hash,
            addr, sizeof(ogs_sockaddr_t));

    return NULL;
}

mme_enb_t *mme_enb_find_by_enb_id(uint32_t enb_id)
{
    return (mme_enb_t *)ogs_hash_get(self.enb_id_hash, &enb_id, sizeof(enb_id));
}

int mme_enb_set_enb_id(mme_enb_t *enb, uint32_t enb_id)
{
    ogs_assert(enb);

    ogs_hash_set(self.enb_id_hash, &enb_id, sizeof(enb_id), NULL);

    enb->enb_id = enb_id;
    ogs_hash_set(self.enb_id_hash, &enb->enb_id, sizeof(enb->enb_id), enb);

    return OGS_OK;
}

int mme_enb_sock_type(ogs_sock_t *sock)
{
    ogs_socknode_t *snode = NULL;

    ogs_assert(sock);

    ogs_list_for_each(&mme_self()->s1ap_list, snode)
        if (snode->sock == sock) return SOCK_SEQPACKET;

    ogs_list_for_each(&mme_self()->s1ap_list6, snode)
        if (snode->sock == sock) return SOCK_SEQPACKET;

    return SOCK_STREAM;
}

mme_enb_t *mme_enb_cycle(mme_enb_t *enb)
{
    return ogs_pool_cycle(&mme_enb_pool, enb);
}

/** enb_ue_context handling function */
enb_ue_t *enb_ue_add(mme_enb_t *enb, uint32_t enb_ue_s1ap_id)
{
    enb_ue_t *enb_ue = NULL;

    ogs_assert(enb);

    ogs_pool_alloc(&enb_ue_pool, &enb_ue);
    if (enb_ue == NULL) {
        ogs_error("Could not allocate enb_ue context from pool");
        return NULL;
    }

    memset(enb_ue, 0, sizeof *enb_ue);

    enb_ue->t_s1_holding = ogs_timer_add(
            ogs_app()->timer_mgr, mme_timer_s1_holding_timer_expire, enb_ue);
    if (!enb_ue->t_s1_holding) {
        ogs_error("ogs_timer_add() failed");
        ogs_pool_free(&enb_ue_pool, enb_ue);
        return NULL;
    }

    enb_ue->index = ogs_pool_index(&enb_ue_pool, enb_ue);
    ogs_assert(enb_ue->index > 0 && enb_ue->index <= ogs_app()->max.ue);

    enb_ue->enb_ue_s1ap_id = enb_ue_s1ap_id;
    enb_ue->mme_ue_s1ap_id = enb_ue->index;

    /*
     * SCTP output stream identification
     * Default ogs_app()->parameter.sctp_streams : 30
     *   0 : Non UE signalling
     *   1-29 : UE specific association
     */
    ogs_assert((enb->max_num_of_ostreams-1) >= 1); /* NEXT_ID(MAX >= MIN) */
    enb_ue->enb_ostream_id =
        OGS_NEXT_ID(enb->ostream_id, 1, enb->max_num_of_ostreams-1);

    enb_ue->enb = enb;

    ogs_list_add(&enb->enb_ue_list, enb_ue);

    stats_add_enb_ue();

    return enb_ue;
}

void enb_ue_remove(enb_ue_t *enb_ue)
{
    mme_enb_t *enb = NULL;

    ogs_assert(enb_ue);
    enb = enb_ue->enb;
    ogs_assert(enb);

    ogs_list_remove(&enb->enb_ue_list, enb_ue);

    ogs_assert(enb_ue->t_s1_holding);
    ogs_timer_delete(enb_ue->t_s1_holding);

    /* Clear enb_ue so if pointer is used again use-after-free is easier to detect */
    memset(enb_ue, 0, sizeof(*enb_ue));

    ogs_pool_free(&enb_ue_pool, enb_ue);

    stats_remove_enb_ue();
}

void enb_ue_switch_to_enb(enb_ue_t *enb_ue, mme_enb_t *new_enb)
{
    ogs_assert(enb_ue);
    ogs_assert(enb_ue->enb);
    ogs_assert(new_enb);

    /* Remove from the old enb */
    ogs_list_remove(&enb_ue->enb->enb_ue_list, enb_ue);

    /* Add to the new enb */
    ogs_list_add(&new_enb->enb_ue_list, enb_ue);

    /* Switch to enb */
    enb_ue->enb = new_enb;
}

enb_ue_t *enb_ue_find_by_enb_ue_s1ap_id(
        mme_enb_t *enb, uint32_t enb_ue_s1ap_id)
{
    enb_ue_t *enb_ue = NULL;

    ogs_list_for_each(&enb->enb_ue_list, enb_ue) {
        if (enb_ue_s1ap_id == enb_ue->enb_ue_s1ap_id)
            break;
    }

    return enb_ue;
}

enb_ue_t *enb_ue_find(uint32_t index)
{
    return ogs_pool_find(&enb_ue_pool, index);
}

enb_ue_t *enb_ue_find_by_mme_ue_s1ap_id(uint32_t mme_ue_s1ap_id)
{
    return enb_ue_find(mme_ue_s1ap_id);
}

enb_ue_t *enb_ue_cycle(enb_ue_t *enb_ue)
{
    return ogs_pool_cycle(&enb_ue_pool, enb_ue);
}

/** sgw_ue_context handling function */
sgw_ue_t *sgw_ue_add(mme_sgw_t *sgw)
{
    sgw_ue_t *sgw_ue = NULL;

    ogs_assert(sgw);

    ogs_pool_alloc(&sgw_ue_pool, &sgw_ue);
    ogs_assert(sgw_ue);
    memset(sgw_ue, 0, sizeof(*sgw_ue));

    sgw_ue->t_s11_holding = ogs_timer_add(
            ogs_app()->timer_mgr, mme_timer_s11_holding_timer_expire, sgw_ue);
    if (!sgw_ue->t_s11_holding) {
        ogs_error("ogs_timer_add() failed");
        ogs_pool_free(&sgw_ue_pool, sgw_ue);
        return NULL;
    }

    sgw_ue->sgw = sgw;

    ogs_list_add(&sgw->sgw_ue_list, sgw_ue);

    return sgw_ue;
}

void sgw_ue_remove(sgw_ue_t *sgw_ue)
{
    mme_sgw_t *sgw = NULL;

    if (NULL == sgw_ue) {
        /* If the sgw_ue was never set we don't need to do anything */
       // ogs_fatal("Trying to remove sgw_ue that doesn't exist!");
        return;
    }

    sgw = sgw_ue->sgw;
    ogs_assert(sgw);

    ogs_list_remove(&sgw->sgw_ue_list, sgw_ue);

    ogs_assert(sgw_ue->t_s11_holding);
    ogs_timer_delete(sgw_ue->t_s11_holding);

    /* Clear sgw_ue so if pointer is used again use-after-free is easier to detect */
    memset(sgw_ue, 0, sizeof(*sgw_ue));

    ogs_pool_free(&sgw_ue_pool, sgw_ue);
}

void sgw_ue_switch_to_sgw(sgw_ue_t *sgw_ue, mme_sgw_t *new_sgw)
{
    ogs_assert(sgw_ue);
    ogs_assert(sgw_ue->sgw);
    ogs_assert(new_sgw);

    /* Remove from the old sgw */
    ogs_list_remove(&sgw_ue->sgw->sgw_ue_list, sgw_ue);

    /* Add to the new sgw */
    ogs_list_add(&new_sgw->sgw_ue_list, sgw_ue);

    /* Switch to sgw */
    sgw_ue->sgw = new_sgw;
}

sgw_ue_t *sgw_ue_cycle(sgw_ue_t *sgw_ue)
{
    return ogs_pool_cycle(&sgw_ue_pool, sgw_ue);
}

sgw_relocation_e sgw_ue_check_if_relocated(mme_ue_t *mme_ue)
{
    enb_ue_t *enb_ue = NULL;
    sgw_ue_t *old_source_ue = NULL, *source_ue = NULL, *target_ue = NULL;
    mme_sgw_t *current = NULL, *changed = NULL;

    ogs_assert(mme_ue);
    enb_ue = enb_ue_cycle(mme_ue->enb_ue);
    ogs_assert(enb_ue);
    source_ue = sgw_ue_cycle(mme_ue->sgw_ue);
    ogs_assert(source_ue);

    current = source_ue->sgw;
    ogs_assert(current);

    changed = changed_sgw_node(current, enb_ue);
    if (!changed) return SGW_WITHOUT_RELOCATION;

    /* Check if Old Source UE */
    old_source_ue = sgw_ue_cycle(source_ue->source_ue);
    if (old_source_ue) {
        sgw_ue_source_deassociate_target(old_source_ue);
        sgw_ue_remove(old_source_ue);
    }

    target_ue = sgw_ue_cycle(source_ue->target_ue);
    if (target_ue) {
        ogs_error("SGW-UE source has already been associated with target");
        return SGW_HAS_ALREADY_BEEN_RELOCATED;
    }

    target_ue = sgw_ue_add(changed);
    ogs_assert(target_ue);

    sgw_ue_source_associate_target(source_ue, target_ue);

    return SGW_WITH_RELOCATION;
}

void mme_ue_new_guti(mme_ue_t *mme_ue)
{
    served_gummei_t *served_gummei = NULL;

    ogs_assert(mme_ue);
    ogs_assert(mme_self()->max_num_of_served_gummei > 0);

    served_gummei = &mme_self()->served_gummei[0];

    ogs_assert(served_gummei->num_of_plmn_id > 0);
    ogs_assert(served_gummei->num_of_mme_gid > 0);
    ogs_assert(served_gummei->num_of_mme_code > 0);

    if (mme_ue->next.m_tmsi) {
        ogs_warn("GUTI has already been allocated");
        return;
    }

    memset(&mme_ue->next.guti, 0, sizeof(ogs_nas_eps_guti_t));

    /* Use the first configured plmn_id and mme group id */
    ogs_nas_from_plmn_id(
            &mme_ue->next.guti.nas_plmn_id, &served_gummei->plmn_id[0]);
    mme_ue->next.guti.mme_gid = served_gummei->mme_gid[0];
    mme_ue->next.guti.mme_code = served_gummei->mme_code[0];

    mme_ue->next.m_tmsi = mme_m_tmsi_alloc();
    ogs_assert(mme_ue->next.m_tmsi);
    mme_ue->next.guti.m_tmsi = *(mme_ue->next.m_tmsi);
}

void mme_ue_confirm_guti(mme_ue_t *mme_ue)
{
    ogs_assert(mme_ue->next.m_tmsi);

    if (mme_ue->current.m_tmsi) {
        /* MME has a VALID GUTI
         * As such, we need to remove previous GUTI in hash table */
        ogs_hash_set(self.guti_ue_hash,
                &mme_ue->current.guti, sizeof(ogs_nas_eps_guti_t), NULL);
        ogs_assert(mme_m_tmsi_free(mme_ue->current.m_tmsi) == OGS_OK);
    }

    /* Copying from Current to Next Guti */
    mme_ue->current.m_tmsi = mme_ue->next.m_tmsi;
    memcpy(&mme_ue->current.guti,
            &mme_ue->next.guti, sizeof(ogs_nas_eps_guti_t));

    /* Hashing Current GUTI */
    ogs_hash_set(self.guti_ue_hash,
            &mme_ue->current.guti, sizeof(ogs_nas_eps_guti_t), mme_ue);

    /* Clear Next GUTI */
    mme_ue->next.m_tmsi = NULL;
}

static bool compare_ue_info(mme_sgw_t *node, enb_ue_t *enb_ue)
{
    int i;

    ogs_assert(node);
    ogs_assert(enb_ue);

    for (i = 0; i < node->num_of_tac; i++)
        if (node->tac[i] == enb_ue->saved.tai.tac) return true;

    for (i = 0; i < node->num_of_e_cell_id; i++)
        if (node->e_cell_id[i] == enb_ue->saved.e_cgi.cell_id) return true;

    return false;
}

static mme_sgw_t *selected_sgw_node(mme_sgw_t *current, enb_ue_t *enb_ue)
{
    char buf[OGS_ADDRSTRLEN];
    mme_sgw_t *next, *node, *random;
    int sgw_count;
    int index;

    ogs_assert(current);
    ogs_assert(enb_ue);

    next = ogs_list_next(current);
    for (node = next; node; node = ogs_list_next(node)) {
        if (compare_ue_info(node, enb_ue) == true) return node;
    }

    for (node = ogs_list_first(&mme_self()->sgw_list);
            node != next; node = ogs_list_next(node)) {
        if (compare_ue_info(node, enb_ue) == true) return node;
    }

    /* Select a random sgw */
    sgw_count = ogs_list_count(&mme_self()->sgw_list);
    index = rand_under(sgw_count);
    ogs_debug("There are %i SGWs in our list, we have randomly picked the one at index %i", sgw_count, index);
    random = ogs_list_at(&mme_self()->sgw_list, index);

    ogs_info(
        "SGWC address chosen was '%s'",
        OGS_ADDR(random->gnode.sa_list, buf)
    );

    return random;
}

static mme_sgw_t *changed_sgw_node(mme_sgw_t *current, enb_ue_t *enb_ue)
{
    mme_sgw_t *changed = NULL;

    ogs_assert(current);
    ogs_assert(enb_ue);

    changed = selected_sgw_node(current, enb_ue);
    if (changed && changed != current &&
        compare_ue_info(changed, enb_ue) == true) return changed;

    return NULL;
}

mme_ue_t *mme_ue_add(enb_ue_t *enb_ue, ogs_nas_eps_message_t *nas_message)
{
    mme_enb_t *enb = NULL;
    mme_ue_t *mme_ue = NULL;
    ogs_nas_mobile_identity_imsi_t *nas_mobile_identity_imsi = NULL; 

    ogs_assert(enb_ue);
    ogs_assert(nas_message);
    enb = enb_ue->enb;
    nas_mobile_identity_imsi =
        &nas_message->emm.attach_request.eps_mobile_identity.imsi;
    ogs_assert(enb);
    ogs_assert(nas_mobile_identity_imsi);

    ogs_pool_alloc(&mme_ue_pool, &mme_ue);
    if (mme_ue == NULL) {
        ogs_error("Could not allocate mme_ue context from pool");
        return NULL;
    }

    memset(mme_ue, 0, sizeof *mme_ue);

    /* Add All Timers */
    mme_ue->t3413.timer = ogs_timer_add(
            ogs_app()->timer_mgr, mme_timer_t3413_expire, mme_ue);
    if (!mme_ue->t3413.timer) {
        ogs_error("ogs_timer_add() failed");
        ogs_pool_free(&mme_ue_pool, mme_ue);
        return NULL;
    }
    mme_ue->t3413.pkbuf = NULL;
    mme_ue->t3422.timer = ogs_timer_add(
            ogs_app()->timer_mgr, mme_timer_t3422_expire, mme_ue);
    if (!mme_ue->t3422.timer) {
        ogs_error("ogs_timer_add() failed");
        ogs_pool_free(&mme_ue_pool, mme_ue);
        return NULL;
    }
    mme_ue->t3422.pkbuf = NULL;
    mme_ue->t3450.timer = ogs_timer_add(
            ogs_app()->timer_mgr, mme_timer_t3450_expire, mme_ue);
    if (!mme_ue->t3450.timer) {
        ogs_error("ogs_timer_add() failed");
        ogs_pool_free(&mme_ue_pool, mme_ue);
        return NULL;
    }
    mme_ue->t3450.pkbuf = NULL;
    mme_ue->t3460.timer = ogs_timer_add(
            ogs_app()->timer_mgr, mme_timer_t3460_expire, mme_ue);
    if (!mme_ue->t3460.timer) {
        ogs_error("ogs_timer_add() failed");
        ogs_pool_free(&mme_ue_pool, mme_ue);
        return NULL;
    }
    mme_ue->t3460.pkbuf = NULL;
    mme_ue->t3470.timer = ogs_timer_add(
            ogs_app()->timer_mgr, mme_timer_t3470_expire, mme_ue);
    if (!mme_ue->t3470.timer) {
        ogs_error("ogs_timer_add() failed");
        ogs_pool_free(&mme_ue_pool, mme_ue);
        return NULL;
    }
    mme_ue->t3470.pkbuf = NULL;
    mme_ue->t_mobile_reachable.timer = ogs_timer_add(
            ogs_app()->timer_mgr, mme_timer_mobile_reachable_expire, mme_ue);
    if (!mme_ue->t_mobile_reachable.timer) {
        ogs_error("ogs_timer_add() failed");
        ogs_pool_free(&mme_ue_pool, mme_ue);
        return NULL;
    }
    mme_ue->t_mobile_reachable.pkbuf = NULL;
    mme_ue->t_implicit_detach.timer = ogs_timer_add(
            ogs_app()->timer_mgr, mme_timer_implicit_detach_expire, mme_ue);
    if (!mme_ue->t_implicit_detach.timer) {
        ogs_error("ogs_timer_add() failed");
        ogs_pool_free(&mme_ue_pool, mme_ue);
        return NULL;
    }
    mme_ue->t_implicit_detach.pkbuf = NULL;

    mme_ebi_pool_init(mme_ue);

    ogs_list_init(&mme_ue->sess_list);

    /* Set MME-S11_TEID */
    ogs_pool_alloc(&mme_s11_teid_pool, &mme_ue->mme_s11_teid_node);
    ogs_assert(mme_ue->mme_s11_teid_node);

    mme_ue->mme_s11_teid = *(mme_ue->mme_s11_teid_node);

    ogs_hash_set(self.mme_s11_teid_hash,
            &mme_ue->mme_s11_teid, sizeof(mme_ue->mme_s11_teid), mme_ue);

    /* SGW selection takes place in mme_s11_build_create_session_request */
    /* PGW selection takes place in mme_s11_build_create_session_request */

    /* Clear VLR */
    mme_ue->csmap = NULL;
    mme_ue->vlr_ostream_id = 0;

    mme_ue_fsm_init(mme_ue);

    ogs_list_add(&self.mme_ue_list, mme_ue);

    ogs_info("[Added] Number of MME-UEs is now %d",
            ogs_list_count(&self.mme_ue_list));

    return mme_ue;
}

void mme_ue_remove(mme_ue_t *mme_ue)
{
    mme_ue = mme_ue_cycle(mme_ue);

    if (NULL == mme_ue) {
        ogs_fatal("Trying to remove mme_ue that doesn't exist!");
        return;
    }

    ogs_list_remove(&self.mme_ue_list, mme_ue);

    mme_ue_fsm_fini(mme_ue);

    ogs_hash_set(self.mme_s11_teid_hash,
            &mme_ue->mme_s11_teid, sizeof(mme_ue->mme_s11_teid), NULL);

    sgw_ue_remove(mme_ue->sgw_ue);

    if (0 != mme_ue->imsi_len) {
        ogs_hash_set(mme_self()->imsi_ue_hash,
                mme_ue->imsi, mme_ue->imsi_len, NULL);
    }

    if (mme_ue->current.m_tmsi) {
        ogs_hash_set(self.guti_ue_hash,
                &mme_ue->current.guti, sizeof(ogs_nas_eps_guti_t), NULL);
        ogs_assert(mme_m_tmsi_free(mme_ue->current.m_tmsi) == OGS_OK);
    }

    if (mme_ue->next.m_tmsi)
        ogs_assert(mme_m_tmsi_free(mme_ue->next.m_tmsi) == OGS_OK);

    /* Clear the saved PDN Connectivity Request */
    OGS_NAS_CLEAR_DATA(&mme_ue->pdn_connectivity_request);

    /* Clear Service Indicator */
    CLEAR_SERVICE_INDICATOR(mme_ue);

    /* Free UeRadioCapability */
    OGS_ASN_CLEAR_DATA(&mme_ue->ueRadioCapability);

    /* Clear Transparent Container */
    OGS_ASN_CLEAR_DATA(&mme_ue->container);

    /* Delete All Timers */
    CLEAR_MME_UE_ALL_TIMERS(mme_ue);
    ogs_timer_delete(mme_ue->t3413.timer);
    ogs_timer_delete(mme_ue->t3422.timer);
    ogs_timer_delete(mme_ue->t3450.timer);
    ogs_timer_delete(mme_ue->t3460.timer);
    ogs_timer_delete(mme_ue->t3470.timer);
    ogs_timer_delete(mme_ue->t_mobile_reachable.timer);
    ogs_timer_delete(mme_ue->t_implicit_detach.timer);

    enb_ue_unlink(mme_ue);

    mme_sess_remove_all(mme_ue);
    mme_session_remove_all(mme_ue);

    mme_ebi_pool_final(mme_ue);

    ogs_pool_free(&mme_s11_teid_pool, mme_ue->mme_s11_teid_node);

    /* Clear mme_ue so if pointer is used again use-after-free is easier to detect */
    memset(mme_ue, 0, sizeof(*mme_ue));

    ogs_pool_free(&mme_ue_pool, mme_ue);

    ogs_info("[Removed] Number of MME-UEs is now %d",
            ogs_list_count(&self.mme_ue_list));
}

void mme_ue_remove_all(void)
{
    mme_ue_t *mme_ue = NULL, *next = NULL;;

    ogs_list_for_each_safe(&self.mme_ue_list, next, mme_ue) {
        enb_ue_t *enb_ue = enb_ue_cycle(mme_ue->enb_ue);

        if (enb_ue) enb_ue_remove(enb_ue);

        mme_ue_remove(mme_ue);
    }
}

mme_ue_t *mme_ue_cycle(mme_ue_t *mme_ue)
{
    return ogs_pool_cycle(&mme_ue_pool, mme_ue);
}

void mme_ue_fsm_init(mme_ue_t *mme_ue)
{
    mme_event_t e;

    ogs_assert(mme_ue);

    memset(&e, 0, sizeof(e));
    e.mme_ue = mme_ue;
    ogs_fsm_init(&mme_ue->sm, emm_state_initial, emm_state_final, &e);
}

void mme_ue_fsm_fini(mme_ue_t *mme_ue)
{
    mme_event_t e;

    ogs_assert(mme_ue);

    memset(&e, 0, sizeof(e));
    e.mme_ue = mme_ue;
    ogs_fsm_fini(&mme_ue->sm, &e);
}

mme_ue_t *mme_ue_find_by_imsi_bcd(char *imsi_bcd)
{
    uint8_t imsi[OGS_MAX_IMSI_LEN];
    int imsi_len = 0;

    ogs_assert(imsi_bcd);

    ogs_bcd_to_buffer(imsi_bcd, imsi, &imsi_len);

    return mme_ue_find_by_imsi(imsi, imsi_len);
}

mme_ue_t *mme_ue_find_by_imsi(uint8_t *imsi, int imsi_len)
{
    ogs_assert(imsi && imsi_len);

    return (mme_ue_t *)ogs_hash_get(self.imsi_ue_hash, imsi, imsi_len);
}

mme_ue_t *mme_ue_find_by_guti(ogs_nas_eps_guti_t *guti)
{
    ogs_assert(guti);

    return (mme_ue_t *)ogs_hash_get(
            self.guti_ue_hash, guti, sizeof(ogs_nas_eps_guti_t));
}

mme_ue_t *mme_ue_find_by_teid(uint32_t teid)
{
    return ogs_hash_get(self.mme_s11_teid_hash, &teid, sizeof(teid));
}

mme_ue_t *mme_ue_find_by_message(ogs_nas_eps_message_t *message)
{
    mme_ue_t *mme_ue = NULL;
    ogs_nas_eps_attach_request_t *attach_request = NULL;
    ogs_nas_eps_detach_request_from_ue_t *detach_request = NULL;
    ogs_nas_eps_tracking_area_update_request_t *tau_request = NULL;
    ogs_nas_eps_extended_service_request_t *extended_service_request = NULL;
    ogs_nas_eps_mobile_identity_t *eps_mobile_identity = NULL;
    ogs_nas_mobile_identity_t *mobile_identity = NULL;

    char imsi_bcd[OGS_MAX_IMSI_BCD_LEN+1];
    ogs_nas_eps_mobile_identity_guti_t *eps_mobile_identity_guti = NULL;
    ogs_nas_mobile_identity_tmsi_t *mobile_identity_tmsi = NULL;
    served_gummei_t *served_gummei = NULL;
    ogs_nas_eps_guti_t ogs_nas_guti;

    switch (message->emm.h.message_type) {
    case OGS_NAS_EPS_ATTACH_REQUEST:
        attach_request = &message->emm.attach_request;
        eps_mobile_identity = &attach_request->eps_mobile_identity;

        switch(eps_mobile_identity->imsi.type) {
        case OGS_NAS_EPS_MOBILE_IDENTITY_IMSI:
            ogs_nas_eps_imsi_to_bcd(
                &eps_mobile_identity->imsi, eps_mobile_identity->length,
                imsi_bcd);

            mme_ue = mme_ue_find_by_imsi_bcd(imsi_bcd);
            if (mme_ue) {
                ogs_info("[%s] known UE by IMSI", imsi_bcd);
            } else {
                ogs_info("[%s] Unknown UE by IMSI", imsi_bcd);
            }
            break;
        case OGS_NAS_EPS_MOBILE_IDENTITY_GUTI:
            eps_mobile_identity_guti = &eps_mobile_identity->guti;

            ogs_nas_guti.nas_plmn_id = eps_mobile_identity_guti->nas_plmn_id;
            ogs_nas_guti.mme_gid = eps_mobile_identity_guti->mme_gid;
            ogs_nas_guti.mme_code = eps_mobile_identity_guti->mme_code;
            ogs_nas_guti.m_tmsi = eps_mobile_identity_guti->m_tmsi;

            mme_ue = mme_ue_find_by_guti(&ogs_nas_guti);
            if (mme_ue) {
                ogs_info("[%s] Known UE by GUTI[G:%d,C:%d,M_TMSI:0x%x]",
                        mme_ue->imsi_bcd,
                        ogs_nas_guti.mme_gid,
                        ogs_nas_guti.mme_code,
                        ogs_nas_guti.m_tmsi);
            } else {
                ogs_info("Unknown UE by GUTI[G:%d,C:%d,M_TMSI:0x%x]",
                        ogs_nas_guti.mme_gid,
                        ogs_nas_guti.mme_code,
                        ogs_nas_guti.m_tmsi);
            }
            break;
        default:
            ogs_error("Unknown EPS Mobile Identity Type [%d]",
                    eps_mobile_identity->imsi.type);
        }
        break;
    case OGS_NAS_EPS_TRACKING_AREA_UPDATE_REQUEST:
        tau_request = &message->emm.tracking_area_update_request;
        eps_mobile_identity = &tau_request->old_guti;

        switch(eps_mobile_identity->imsi.type) {
        case OGS_NAS_EPS_MOBILE_IDENTITY_GUTI:
            eps_mobile_identity_guti = &eps_mobile_identity->guti;

            ogs_nas_guti.nas_plmn_id = eps_mobile_identity_guti->nas_plmn_id;
            ogs_nas_guti.mme_gid = eps_mobile_identity_guti->mme_gid;
            ogs_nas_guti.mme_code = eps_mobile_identity_guti->mme_code;
            ogs_nas_guti.m_tmsi = eps_mobile_identity_guti->m_tmsi;

            mme_ue = mme_ue_find_by_guti(&ogs_nas_guti);
            if (mme_ue) {
                ogs_info("[%s] Known UE by GUTI[G:%d,C:%d,M_TMSI:0x%x]",
                        mme_ue->imsi_bcd,
                        ogs_nas_guti.mme_gid,
                        ogs_nas_guti.mme_code,
                        ogs_nas_guti.m_tmsi);
            } else {
                ogs_info("Unknown UE by GUTI[G:%d,C:%d,M_TMSI:0x%x]",
                        ogs_nas_guti.mme_gid,
                        ogs_nas_guti.mme_code,
                        ogs_nas_guti.m_tmsi);
            }
            break;
        default:
            ogs_error("Unknown EPS Mobile Identity Type [%d]",
                    eps_mobile_identity->imsi.type);
        }
        break;
    case OGS_NAS_EPS_DETACH_REQUEST:
        detach_request = &message->emm.detach_request_from_ue;
        eps_mobile_identity = &detach_request->eps_mobile_identity;

        switch(eps_mobile_identity->imsi.type) {
        case OGS_NAS_EPS_MOBILE_IDENTITY_GUTI:
            eps_mobile_identity_guti = &eps_mobile_identity->guti;

            ogs_nas_guti.nas_plmn_id = eps_mobile_identity_guti->nas_plmn_id;
            ogs_nas_guti.mme_gid = eps_mobile_identity_guti->mme_gid;
            ogs_nas_guti.mme_code = eps_mobile_identity_guti->mme_code;
            ogs_nas_guti.m_tmsi = eps_mobile_identity_guti->m_tmsi;

            mme_ue = mme_ue_find_by_guti(&ogs_nas_guti);
            if (mme_ue) {
                ogs_info("[%s] Known UE by GUTI[G:%d,C:%d,M_TMSI:0x%x]",
                        mme_ue->imsi_bcd,
                        ogs_nas_guti.mme_gid,
                        ogs_nas_guti.mme_code,
                        ogs_nas_guti.m_tmsi);
            } else {
                ogs_info("Unknown UE by GUTI[G:%d,C:%d,M_TMSI:0x%x]",
                        ogs_nas_guti.mme_gid,
                        ogs_nas_guti.mme_code,
                        ogs_nas_guti.m_tmsi);
            }
            break;
        default:
            ogs_error("Unknown EPS Mobile Identity Type [%d]",
                    eps_mobile_identity->imsi.type);
        }
        break;
    case OGS_NAS_EPS_EXTENDED_SERVICE_REQUEST:
        extended_service_request = &message->emm.extended_service_request;
        mobile_identity = &extended_service_request->m_tmsi;

        switch(mobile_identity->tmsi.type) {
        case OGS_NAS_MOBILE_IDENTITY_TMSI:
            mobile_identity_tmsi = &mobile_identity->tmsi;
            served_gummei = &mme_self()->served_gummei[0];

            /* Use the first configured plmn_id and mme group id */
            ogs_nas_from_plmn_id(
                    &ogs_nas_guti.nas_plmn_id, &served_gummei->plmn_id[0]);
            ogs_nas_guti.mme_gid = served_gummei->mme_gid[0];
            ogs_nas_guti.mme_code = served_gummei->mme_code[0];
            ogs_nas_guti.m_tmsi = mobile_identity_tmsi->tmsi;

            mme_ue = mme_ue_find_by_guti(&ogs_nas_guti);
            if (mme_ue) {
                ogs_info("[%s] Known UE by GUTI[G:%d,C:%d,M_TMSI:0x%x]",
                        mme_ue->imsi_bcd,
                        ogs_nas_guti.mme_gid,
                        ogs_nas_guti.mme_code,
                        ogs_nas_guti.m_tmsi);
            } else {
                ogs_info("Unknown UE by GUTI[G:%d,C:%d,M_TMSI:0x%x]",
                        ogs_nas_guti.mme_gid,
                        ogs_nas_guti.mme_code,
                        ogs_nas_guti.m_tmsi);
            }
            break;
        default:
            ogs_error("Unknown Mobile Identity Type [%d]",
                    mobile_identity->tmsi.type);
        }
        break;
    default:
        break;
    }

    return mme_ue;
}

int mme_ue_set_imsi(mme_ue_t *mme_ue, char *imsi_bcd)
{
    mme_ue_t *old_mme_ue = NULL;
    mme_sess_t *old_sess = NULL;
    mme_bearer_t *old_bearer = NULL;
    ogs_assert(mme_ue && imsi_bcd);

    ogs_cpystrn(mme_ue->imsi_bcd, imsi_bcd, OGS_MAX_IMSI_BCD_LEN+1);
    ogs_bcd_to_buffer(mme_ue->imsi_bcd, mme_ue->imsi, &mme_ue->imsi_len);

    /* Check if OLD mme_ue_t is existed */
    old_mme_ue = mme_ue_find_by_imsi(mme_ue->imsi, mme_ue->imsi_len);
    if (old_mme_ue) {
        /* Check if OLD mme_ue_t is different with NEW mme_ue_t */
        if (ogs_pool_index(&mme_ue_pool, mme_ue) !=
            ogs_pool_index(&mme_ue_pool, old_mme_ue)) {
            ogs_warn("[%s] OLD UE Context Release", mme_ue->imsi_bcd);
            if (ECM_CONNECTED(old_mme_ue)) {
                /* Implcit S1 release */
                ogs_info("[%s] Implicit S1 release", mme_ue->imsi_bcd);
                ogs_info("[%s]    ENB_UE_S1AP_ID[%d] MME_UE_S1AP_ID[%d]",
                        old_mme_ue->imsi_bcd,
                        old_mme_ue->enb_ue->enb_ue_s1ap_id,
                        old_mme_ue->enb_ue->mme_ue_s1ap_id);
                enb_ue_remove(old_mme_ue->enb_ue);
            }

    /*
     * We should delete the MME-Session Context in the MME-UE Context.
     * Otherwise, all unnecessary SESSIONs remain in SMF/UPF.
     *
     * In order to do this, MME-Session Context should be moved
     * from OLD MME-UE Context to NEW MME-UE Context.
     *
     * If needed, The Session deletion process in NEW-MME UE context will work.
     *
     * Note that we should not send Session-Release to the SGW-C at this point.
     * Another GTPv2-C Transaction can cause fatal errors.
     */
            /* Phase-1 : Change MME-UE Context in Session Context */
            ogs_list_for_each(&old_mme_ue->sess_list, old_sess) {
                ogs_list_for_each(&old_sess->bearer_list, old_bearer) {
                    old_bearer->mme_ue = mme_ue;

                    if (old_bearer->ebi_node)
                        ogs_pool_free(
                                &old_mme_ue->ebi_pool, old_bearer->ebi_node);
                    old_bearer->ebi_node = NULL;
                }
                old_sess->mme_ue = mme_ue;
            }

            /* Phase-2 : Move Session Context from OLD to NEW MME-UE Context */
            memcpy(&mme_ue->sess_list,
                    &old_mme_ue->sess_list, sizeof(mme_ue->sess_list));

            /* Phase-3 : Clear Session Context in OLD MME-UE Context */
            memset(&old_mme_ue->sess_list, 0, sizeof(old_mme_ue->sess_list));

            /* Phase-4 : Move sgw_ue->sgw_s11_teid if possible */
            if (NULL == old_mme_ue->sgw_ue) {
                /* If the old_mme_ue doesn't have a sgw_ue it 
                 * must not have received a Create Session Response.
                 * In this case we dont need to do anything as the new
                 * mme_ue will get a fresh sgw_ue when the Create Session
                 * Request is sent */
                ogs_debug("old_mme_ue->sgw_ue does not exist");
            } else if (NULL == mme_ue->sgw_ue) {
                /* If the new mme_ue doesn't have a sgw_ue a
                 * Create Session Request hasn't been sent yet.
                 * At this point we know that the old sgw_ue exists
                 * so lets take that one. After copying the sgw_ue
                 * we unlink the old_mme_ue's sgw_ue so that the
                 * call to mme_ue_remove doesn't free up that sgw_ue 
                 * memory. */
                ogs_debug("old_mme_ue->sgw_ue exists but mme_ue->sgw_ue does not");
                sgw_ue_associate_mme_ue(old_mme_ue->sgw_ue, mme_ue);
                sgw_ue_unlink(old_mme_ue);
            } else {
                /* If a new connection exists (mme_ue->sgw_ue != NULL)
                 * then we need to remove the old connection */
                ogs_error("New and old sgw connections (sgw_ue) found for imsi '%s', removing the old and keeping the new", imsi_bcd);
            }

            mme_ue_remove(old_mme_ue);
        }
    }

    if (mme_ue->imsi_len != 0)
        ogs_hash_set(mme_self()->imsi_ue_hash,
                mme_ue->imsi, mme_ue->imsi_len, NULL);

    ogs_hash_set(self.imsi_ue_hash, mme_ue->imsi, mme_ue->imsi_len, mme_ue);
	
    mme_ue->hssmap = mme_hssmap_find_by_imsi_bcd(mme_ue->imsi_bcd);
    if (mme_ue->hssmap) {
        char plmn_id_str[OGS_PLMNIDSTRLEN];
        const char *realm = mme_ue->hssmap->realm ? mme_ue->hssmap->realm : "NULL";
        const char *host = mme_ue->hssmap->host ? mme_ue->hssmap->host : "NULL";

        ogs_plmn_id_to_string(&mme_ue->hssmap->plmn_id, plmn_id_str);
        ogs_debug("[%s]: HSS Map HPLMN[%s] Realm[%s] Host[%s]",
                   mme_ue->imsi_bcd, plmn_id_str, realm, host);

    }


    return OGS_OK;
}

bool mme_ue_have_indirect_tunnel(mme_ue_t *mme_ue)
{
    mme_sess_t *sess = NULL;
    mme_bearer_t *bearer = NULL;

    ogs_assert(mme_ue);

    ogs_list_for_each(&mme_ue->sess_list, sess) {
        ogs_list_for_each(&sess->bearer_list, bearer) {
            if (MME_HAVE_ENB_DL_INDIRECT_TUNNEL(bearer) ||
                MME_HAVE_ENB_UL_INDIRECT_TUNNEL(bearer) ||
                MME_HAVE_SGW_DL_INDIRECT_TUNNEL(bearer) ||
                MME_HAVE_SGW_UL_INDIRECT_TUNNEL(bearer)) {
                return true;
            }
        }
    }

    return false;
}

void mme_ue_clear_indirect_tunnel(mme_ue_t *mme_ue)
{
    mme_sess_t *sess = NULL;
    mme_bearer_t *bearer = NULL;

    ogs_assert(mme_ue);

    ogs_list_for_each(&mme_ue->sess_list, sess) {
        ogs_list_for_each(&sess->bearer_list, bearer) {
            CLEAR_INDIRECT_TUNNEL(bearer);
        }
    }
}

bool mme_ue_have_active_eps_bearers(mme_ue_t *mme_ue)
{
    mme_sess_t *sess = NULL;

    ogs_assert(mme_ue);

    ogs_list_for_each(&mme_ue->sess_list, sess) {
        if (mme_sess_have_active_eps_bearers(sess) == true)
            return true;
    }

    return false;
}

bool mme_sess_have_active_eps_bearers(mme_sess_t *sess)
{
    mme_bearer_t *bearer = NULL;
    ogs_assert(sess);

    ogs_list_for_each(&sess->bearer_list, bearer) {
        if (OGS_FSM_CHECK(&bearer->sm, esm_state_active))
            return true;
    }

    return false;
}

bool mme_ue_have_session_release_pending(mme_ue_t *mme_ue)
{
    mme_sess_t *sess = NULL;

    ogs_assert(mme_ue);

    ogs_list_for_each(&mme_ue->sess_list, sess) {
        if (mme_sess_have_session_release_pending(sess) == true)
            return true;
    }

    return false;
}

bool mme_sess_have_session_release_pending(mme_sess_t *sess)
{
    mme_bearer_t *bearer = NULL;
    ogs_assert(sess);

    ogs_list_for_each(&sess->bearer_list, bearer) {
        if (OGS_FSM_CHECK(&bearer->sm, esm_state_pdn_will_disconnect))
            return true;
    }

    return false;
}

int mme_ue_xact_count(mme_ue_t *mme_ue, uint8_t org)
{
    sgw_ue_t *sgw_ue = NULL;
    ogs_gtp_node_t *gnode = NULL;

    ogs_assert(org == OGS_GTP_LOCAL_ORIGINATOR ||
                org == OGS_GTP_REMOTE_ORIGINATOR);

    sgw_ue = mme_ue->sgw_ue;
    if (!sgw_ue) return 0;

    gnode = sgw_ue->gnode;
    if (!gnode) return 0;

    return org == OGS_GTP_LOCAL_ORIGINATOR ?
            ogs_list_count(&gnode->local_list) :
                ogs_list_count(&gnode->remote_list);
}

bool imsi_is_roaming(ogs_nas_mobile_identity_imsi_t *nas_imsi)
{
    ogs_assert(nas_imsi);
    
    if (OGS_NAS_EPS_MOBILE_IDENTITY_IMSI != nas_imsi->type) {
        /* In this case we need to wait for emm_handle_identity_response to get the imsi */
        ogs_warn("eps_mobile_identity type was not OGS_NAS_EPS_MOBILE_IDENTITY_IMSI, assuming not roaming for now");
        return false;
    }
    
    uint16_t ue_mcc = 100 * nas_imsi->digit1 +
                      10 * nas_imsi->digit2 +
                      1 * nas_imsi->digit3;

    uint16_t ue_mnc_3_digit = 100 * nas_imsi->digit4 +
                              10 * nas_imsi->digit5 +
                              1 * nas_imsi->digit6;

    uint16_t ue_mnc_2_digit = 10 * nas_imsi->digit4 +
                              1 * nas_imsi->digit5;


    for (int i = 0; i < mme_self()->home_mnc_mcc_sz; ++i) {
        uint16_t home_mcc = mme_self()->home_mnc_mcc[i].mcc;
        uint16_t home_mnc = mme_self()->home_mnc_mcc[i].mnc;

        if (ue_mcc == home_mcc)
        {
            if ((ue_mnc_2_digit == home_mnc) ||
                (ue_mnc_3_digit == home_mnc)) 
            {
                /* Is not roaming */
    		    return false;
            }
        }
    }

    /* Must be roaming */
    return true;
}

void enb_ue_associate_mme_ue(enb_ue_t *enb_ue, mme_ue_t *mme_ue)
{
    ogs_assert(mme_ue);
    ogs_assert(enb_ue);

    mme_ue->enb_ue = enb_ue;
    enb_ue->mme_ue = mme_ue;
}

void enb_ue_deassociate(enb_ue_t *enb_ue)
{
    ogs_assert(enb_ue);
    enb_ue->mme_ue = NULL;
}

void enb_ue_unlink(mme_ue_t *mme_ue)
{
    ogs_assert(mme_ue);
    mme_ue->enb_ue = NULL;
}

void enb_ue_source_associate_target(enb_ue_t *source_ue, enb_ue_t *target_ue)
{
    mme_ue_t *mme_ue = NULL;

    ogs_assert(source_ue);
    ogs_assert(target_ue);
    mme_ue = source_ue->mme_ue;
    ogs_assert(mme_ue);

    target_ue->mme_ue = mme_ue;
    target_ue->source_ue = source_ue;
    source_ue->target_ue = target_ue;
}

void enb_ue_source_deassociate_target(enb_ue_t *enb_ue)
{
    enb_ue_t *source_ue = NULL;
    enb_ue_t *target_ue = NULL;
    ogs_assert(enb_ue);

    if (enb_ue->target_ue) {
        source_ue = enb_ue;
        target_ue = enb_ue->target_ue;

        ogs_assert(source_ue->target_ue);
        ogs_assert(target_ue->source_ue);
        source_ue->target_ue = NULL;
        target_ue->source_ue = NULL;
    } else if (enb_ue->source_ue) {
        target_ue = enb_ue;
        source_ue = enb_ue->source_ue;

        ogs_assert(source_ue->target_ue);
        ogs_assert(target_ue->source_ue);
        source_ue->target_ue = NULL;
        target_ue->source_ue = NULL;
    }
}

void sgw_ue_associate_mme_ue(sgw_ue_t *sgw_ue, mme_ue_t *mme_ue)
{
    ogs_assert(mme_ue);
    ogs_assert(sgw_ue);

    mme_ue->sgw_ue = sgw_ue;
    sgw_ue->mme_ue = mme_ue;
}

void sgw_ue_deassociate(sgw_ue_t *sgw_ue)
{
    ogs_assert(sgw_ue);
    sgw_ue->mme_ue = NULL;
}

void sgw_ue_unlink(mme_ue_t *mme_ue)
{
    ogs_assert(mme_ue);
    mme_ue->sgw_ue = NULL;
}

void sgw_ue_source_associate_target(sgw_ue_t *source_ue, sgw_ue_t *target_ue)
{
    mme_ue_t *mme_ue = NULL;

    ogs_assert(source_ue);
    ogs_assert(target_ue);
    mme_ue = source_ue->mme_ue;
    ogs_assert(mme_ue);

    target_ue->mme_ue = mme_ue;
    target_ue->source_ue = source_ue;
    source_ue->target_ue = target_ue;
}

void sgw_ue_source_deassociate_target(sgw_ue_t *sgw_ue)
{
    sgw_ue_t *source_ue = NULL;
    sgw_ue_t *target_ue = NULL;
    ogs_assert(sgw_ue);

    if (sgw_ue->target_ue) {
        source_ue = sgw_ue;
        target_ue = sgw_ue->target_ue;

        ogs_assert(source_ue->target_ue);
        ogs_assert(target_ue->source_ue);
        source_ue->target_ue = NULL;
        target_ue->source_ue = NULL;
    } else if (sgw_ue->source_ue) {
        target_ue = sgw_ue;
        source_ue = sgw_ue->source_ue;

        ogs_assert(source_ue->target_ue);
        ogs_assert(target_ue->source_ue);
        source_ue->target_ue = NULL;
        target_ue->source_ue = NULL;
    }
}

mme_sess_t *mme_sess_add(mme_ue_t *mme_ue, uint8_t pti)
{
    mme_sess_t *sess = NULL;
    mme_bearer_t *bearer = NULL;

    ogs_assert(mme_ue);
    ogs_assert(pti != OGS_NAS_PROCEDURE_TRANSACTION_IDENTITY_UNASSIGNED);

    ogs_pool_alloc(&mme_sess_pool, &sess);
    ogs_assert(sess);
    memset(sess, 0, sizeof *sess);

    ogs_list_init(&sess->bearer_list);

    sess->mme_ue = mme_ue;
    sess->pti = pti;

    bearer = mme_bearer_add(sess);
    ogs_assert(bearer);

    ogs_list_add(&mme_ue->sess_list, sess);

    stats_add_mme_session();

    return sess;
}

void mme_sess_remove(mme_sess_t *sess)
{
    mme_ue_t *mme_ue = NULL;

    sess = mme_sess_cycle(sess);

    if (NULL == sess) {
        ogs_error("Trying to remove a sess that doesnt exist!");
        return;
    }

    if ((NULL == sess->session) ||
        (NULL == sess->session->name)) {
        ogs_error("Session information was NULL, could not check if we needed to decrement MME_METR_GLOB_GAUGE_EMERGENCY_BEARERS gauge");
    } else if (!strcmp("sos", sess->session->name)) {
        mme_metrics_inst_global_dec(MME_METR_GLOB_GAUGE_EMERGENCY_BEARERS);
    }

    mme_ue = mme_ue_cycle(sess->mme_ue);
    if (NULL != mme_ue) {
        ogs_list_remove(&mme_ue->sess_list, sess);
    } else {
        ogs_error("Sess didn't have an associated mme_ue");
    }

    mme_bearer_remove_all(sess);

    OGS_NAS_CLEAR_DATA(&sess->ue_pco);
    OGS_NAS_CLEAR_DATA(&sess->ue_epco);
    OGS_TLV_CLEAR_DATA(&sess->pgw_pco);
    OGS_TLV_CLEAR_DATA(&sess->pgw_epco);

    /* Clear sess so if pointer is used again use-after-free is easier to detect */
    memset(sess, 0, sizeof(*sess));

    ogs_pool_free(&mme_sess_pool, sess);

    stats_remove_mme_session();
}

void mme_sess_remove_all(mme_ue_t *mme_ue)
{
    mme_sess_t *sess = NULL, *next_sess = NULL;

    mme_ue = mme_ue_cycle(mme_ue);
    if (NULL == mme_ue) {
        ogs_error("Trying to remove all sess from mme_ue that doesn't exist!");
        return;
    }

    sess = mme_sess_first(mme_ue);
    while (sess) {
        next_sess = mme_sess_next(sess);

        mme_sess_remove(sess);

        sess = next_sess;
    }
}

mme_sess_t *mme_sess_find_by_pti(mme_ue_t *mme_ue, uint8_t pti)
{
    mme_sess_t *sess = NULL;

    sess = mme_sess_first(mme_ue);
    while(sess) {
        if (pti == sess->pti)
            return sess;

        sess = mme_sess_next(sess);
    }

    return NULL;
}

mme_sess_t *mme_sess_find_by_ebi(mme_ue_t *mme_ue, uint8_t ebi)
{
    mme_bearer_t *bearer = NULL;

    bearer = mme_bearer_find_by_ue_ebi(mme_ue, ebi);
    if (bearer)
        return bearer->sess;

    return NULL;
}

mme_sess_t *mme_sess_find_by_apn(mme_ue_t *mme_ue, char *apn)
{
    mme_sess_t *sess = NULL;

    sess = mme_sess_first(mme_ue);
    while (sess) {
        ogs_assert(sess->session->name);
        if (sess->session && ogs_strcasecmp(sess->session->name, apn) == 0)
            return sess;

        sess = mme_sess_next(sess);
    }

    return NULL;
}

mme_sess_t *mme_sess_first(mme_ue_t *mme_ue)
{
    return ogs_list_first(&mme_ue->sess_list);
}

mme_sess_t *mme_sess_next(mme_sess_t *sess)
{
    return ogs_list_next(sess);
}

unsigned int mme_sess_count(mme_ue_t *mme_ue)
{
    unsigned int count = 0;
    mme_sess_t *sess = NULL;

    ogs_list_for_each(&mme_ue->sess_list, sess) {
        count++;
    }

    return count;
}

mme_sess_t *mme_sess_cycle(mme_sess_t *sess)
{
    return ogs_pool_cycle(&mme_sess_pool, sess);
}

mme_bearer_t *mme_bearer_add(mme_sess_t *sess)
{
    mme_event_t e;

    mme_bearer_t *bearer = NULL;
    mme_ue_t *mme_ue = NULL;

    ogs_assert(sess);
    mme_ue = sess->mme_ue;
    ogs_assert(mme_ue);

    ogs_pool_alloc(&mme_bearer_pool, &bearer);
    ogs_assert(bearer);
    memset(bearer, 0, sizeof *bearer);

    ogs_pool_alloc(&mme_ue->ebi_pool, &bearer->ebi_node);
    ogs_assert(bearer->ebi_node);

    bearer->ebi = *(bearer->ebi_node);

    ogs_assert(bearer->ebi >= MIN_EPS_BEARER_ID &&
                bearer->ebi <= MAX_EPS_BEARER_ID);

    bearer->mme_ue = mme_ue;
    bearer->sess = sess;

    ogs_list_add(&sess->bearer_list, bearer);

    bearer->t3489.timer = ogs_timer_add(
            ogs_app()->timer_mgr, mme_timer_t3489_expire, bearer);
    bearer->t3489.pkbuf = NULL;

    memset(&e, 0, sizeof(e));
    e.bearer = bearer;
    ogs_fsm_init(&bearer->sm, esm_state_initial, esm_state_final, &e);

    return bearer;
}

void mme_bearer_remove(mme_bearer_t *bearer)
{
    mme_event_t e;

    ogs_assert(bearer);
    ogs_assert(bearer->mme_ue);
    ogs_assert(bearer->sess);

    memset(&e, 0, sizeof(e));
    e.bearer = bearer;
    ogs_fsm_fini(&bearer->sm, &e);

    CLEAR_BEARER_ALL_TIMERS(bearer);
    ogs_timer_delete(bearer->t3489.timer);

    ogs_list_remove(&bearer->sess->bearer_list, bearer);

    OGS_TLV_CLEAR_DATA(&bearer->tft);

    if (bearer->ebi_node)
        ogs_pool_free(&bearer->mme_ue->ebi_pool, bearer->ebi_node);

    /* Clear bearer so if pointer is used again use-after-free is easier to detect */
    memset(bearer, 0, sizeof(*bearer));

    ogs_pool_free(&mme_bearer_pool, bearer);
}

void mme_bearer_remove_all(mme_sess_t *sess)
{
    mme_bearer_t *bearer = NULL, *next_bearer = NULL;

    ogs_assert(sess);

    bearer = mme_bearer_first(sess);
    while (bearer) {
        next_bearer = mme_bearer_next(bearer);

        mme_bearer_remove(bearer);

        bearer = next_bearer;
    }
}

mme_bearer_t *mme_bearer_find_by_sess_ebi(mme_sess_t *sess, uint8_t ebi)
{
    mme_bearer_t *bearer = NULL;

    ogs_assert(sess);

    bearer = mme_bearer_first(sess);
    while (bearer) {
        if (ebi == bearer->ebi)
            return bearer;

        bearer = mme_bearer_next(bearer);
    }

    return NULL;
}

mme_bearer_t *mme_bearer_find_by_ue_ebi(mme_ue_t *mme_ue, uint8_t ebi)
{
    mme_sess_t *sess = NULL;
    mme_bearer_t *bearer = NULL;

    ogs_assert(mme_ue);

    sess = mme_sess_first(mme_ue);
    while (sess) {
        bearer = mme_bearer_find_by_sess_ebi(sess, ebi);
        if (bearer) {
            return bearer;
        }

        sess = mme_sess_next(sess);
    }

    return NULL;
}

mme_bearer_t *mme_bearer_find_or_add_by_message(
        mme_ue_t *mme_ue, ogs_nas_eps_message_t *message, int create_action)
{
    int r;
    uint8_t pti = OGS_NAS_PROCEDURE_TRANSACTION_IDENTITY_UNASSIGNED;
    uint8_t ebi = OGS_NAS_EPS_BEARER_IDENTITY_UNASSIGNED;

    mme_bearer_t *bearer = NULL;
    mme_sess_t *sess = NULL;

    ogs_assert(mme_ue);
    ogs_assert(message);

    pti = message->esm.h.procedure_transaction_identity;
    ebi = message->esm.h.eps_bearer_identity;

    ogs_debug("mme_bearer_find_or_add_by_message() [PTI:%d, EBI:%d]",
            pti, ebi);

    if (ebi != OGS_NAS_EPS_BEARER_IDENTITY_UNASSIGNED) {
        bearer = mme_bearer_find_by_ue_ebi(mme_ue, ebi);
        if (!bearer) {
            ogs_error("No Bearer : EBI[%d]", ebi);
            r = nas_eps_send_attach_reject(mme_ue,
                    OGS_NAS_EMM_CAUSE_PROTOCOL_ERROR_UNSPECIFIED,
                    OGS_NAS_ESM_CAUSE_PROTOCOL_ERROR_UNSPECIFIED);
            ogs_expect(r == OGS_OK);
            ogs_assert(r != OGS_ERROR);
            return NULL;
        }

        return bearer;
    }

    if (pti == OGS_NAS_PROCEDURE_TRANSACTION_IDENTITY_UNASSIGNED) {
        ogs_error("Both PTI[%d] and EBI[%d] are 0", pti, ebi);
        r = nas_eps_send_attach_reject(mme_ue,
                OGS_NAS_EMM_CAUSE_PROTOCOL_ERROR_UNSPECIFIED,
                OGS_NAS_ESM_CAUSE_PROTOCOL_ERROR_UNSPECIFIED);
        ogs_expect(r == OGS_OK);
        ogs_assert(r != OGS_ERROR);
        return NULL;
    }

    if (message->esm.h.message_type == OGS_NAS_EPS_PDN_DISCONNECT_REQUEST) {
        ogs_nas_eps_pdn_disconnect_request_t *pdn_disconnect_request =
            &message->esm.pdn_disconnect_request;
        ogs_nas_linked_eps_bearer_identity_t *linked_eps_bearer_identity =
            &pdn_disconnect_request->linked_eps_bearer_identity;

        bearer = mme_bearer_find_by_ue_ebi(mme_ue,
                linked_eps_bearer_identity->eps_bearer_identity);
        if (!bearer) {
            ogs_error("No Bearer : Linked-EBI[%d]",
                    linked_eps_bearer_identity->eps_bearer_identity);
            r = nas_eps_send_attach_reject(mme_ue,
                    OGS_NAS_EMM_CAUSE_PROTOCOL_ERROR_UNSPECIFIED,
                    OGS_NAS_ESM_CAUSE_PROTOCOL_ERROR_UNSPECIFIED);
            ogs_expect(r == OGS_OK);
            ogs_assert(r != OGS_ERROR);
            return NULL;
        }
    } else if (message->esm.h.message_type ==
            OGS_NAS_EPS_BEARER_RESOURCE_ALLOCATION_REQUEST) {
        ogs_nas_eps_bearer_resource_allocation_request_t
            *bearer_allocation_request =
                &message->esm.bearer_resource_allocation_request;
        ogs_nas_linked_eps_bearer_identity_t *linked_eps_bearer_identity =
            &bearer_allocation_request->linked_eps_bearer_identity;

        bearer = mme_bearer_find_by_ue_ebi(mme_ue,
                linked_eps_bearer_identity->eps_bearer_identity);
        if (!bearer) {
            ogs_error("No Bearer : Linked-EBI[%d]",
                    linked_eps_bearer_identity->eps_bearer_identity);
            r = nas_eps_send_bearer_resource_allocation_reject(
                    mme_ue, pti,
                    OGS_NAS_ESM_CAUSE_INVALID_EPS_BEARER_IDENTITY);
            ogs_expect(r == OGS_OK);
            ogs_assert(r != OGS_ERROR);
            return NULL;
        }

    } else if (message->esm.h.message_type ==
            OGS_NAS_EPS_BEARER_RESOURCE_MODIFICATION_REQUEST) {
        ogs_nas_eps_bearer_resource_modification_request_t
            *bearer_modification_request =
                &message->esm.bearer_resource_modification_request;
        ogs_nas_linked_eps_bearer_identity_t *linked_eps_bearer_identity =
            &bearer_modification_request->eps_bearer_identity_for_packet_filter;

        bearer = mme_bearer_find_by_ue_ebi(mme_ue,
                linked_eps_bearer_identity->eps_bearer_identity);
        if (!bearer) {
            ogs_error("No Bearer : Linked-EBI[%d]",
                    linked_eps_bearer_identity->eps_bearer_identity);
            r = nas_eps_send_bearer_resource_modification_reject(
                    mme_ue, pti,
                    OGS_NAS_ESM_CAUSE_INVALID_EPS_BEARER_IDENTITY);
            ogs_expect(r == OGS_OK);
            ogs_assert(r != OGS_ERROR);
            return NULL;
        }
    }

    if (bearer) {
        sess = bearer->sess;
        ogs_assert(sess);
        sess->pti = pti;

        return bearer;
    }

    if (message->esm.h.message_type == OGS_NAS_EPS_PDN_CONNECTIVITY_REQUEST) {
        ogs_nas_eps_pdn_connectivity_request_t *pdn_connectivity_request =
            &message->esm.pdn_connectivity_request;

        if (OGS_NAS_EPS_REQUEST_TYPE_EMERGENCY == pdn_connectivity_request->request_type.value) {
            /* Special case, make sure we don't get duplicate sos APNs */
            char sos[] = "sos";
            sess = mme_sess_find_by_apn(mme_ue, sos);
            if (sess && create_action != OGS_GTP_CREATE_IN_ATTACH_REQUEST) {
                /* Duplicate APNs are handled by SGW & PGW.
                 * If the new Create Session Request received 
                 * by the SGW collides with an existing active 
                 * PDN connection context, this Create Session
                 * Request shall be treated as a request for a
                 * new session. */
                ogs_warn("APN duplication detected [%s]", sos);
            }
        } else if (pdn_connectivity_request->presencemask &
            OGS_NAS_EPS_PDN_CONNECTIVITY_REQUEST_ACCESS_POINT_NAME_PRESENT) {
            /* Specific APN has been requested by the UE, make sure it doesn't already exist */
            sess = mme_sess_find_by_apn(mme_ue,
                    pdn_connectivity_request->access_point_name.apn);
            if (sess && create_action != OGS_GTP_CREATE_IN_ATTACH_REQUEST) {
                /* Duplicate APNs are handled by SGW & PGW.
                 * If the new Create Session Request received 
                 * by the SGW collides with an existing active 
                 * PDN connection context, this Create Session
                 * Request shall be treated as a request for a
                 * new session. */
                ogs_warn("APN duplication detected [%s]",
                    pdn_connectivity_request->access_point_name.apn);
            }
        } else {
            /* Default case, session assumed to be the first session in list */
            sess = mme_sess_first(mme_ue);
        }

        if (!sess)
            sess = mme_sess_add(mme_ue, pti);
        else
            sess->pti = pti;

        ogs_assert(sess);
    } else {
        sess = mme_sess_find_by_pti(mme_ue, pti);
        if (!sess) {
            ogs_error("No Session : ESM message type[%d], PTI[%d]",
                    message->esm.h.message_type, pti);
            r = nas_eps_send_attach_reject(mme_ue,
                    OGS_NAS_EMM_CAUSE_PROTOCOL_ERROR_UNSPECIFIED,
                    OGS_NAS_ESM_CAUSE_PROTOCOL_ERROR_UNSPECIFIED);
            ogs_expect(r == OGS_OK);
            ogs_assert(r != OGS_ERROR);
            return NULL;
        }
    }

    bearer = mme_default_bearer_in_sess(sess);
    ogs_assert(bearer);

    return bearer;
}

mme_bearer_t *mme_default_bearer_in_sess(mme_sess_t *sess)
{
    ogs_assert(sess);
    return mme_bearer_first(sess);
}

mme_bearer_t *mme_linked_bearer(mme_bearer_t *bearer)
{
    mme_sess_t *sess = NULL;

    ogs_assert(bearer);
    sess = bearer->sess;
    ogs_assert(sess);

    return mme_default_bearer_in_sess(sess);
}

mme_bearer_t *mme_bearer_first(mme_sess_t *sess)
{
    ogs_assert(sess);

    return ogs_list_first(&sess->bearer_list);
}

mme_bearer_t *mme_bearer_next(mme_bearer_t *bearer)
{
    ogs_assert(bearer);
    return ogs_list_next(bearer);
}

mme_bearer_t *mme_bearer_cycle(mme_bearer_t *bearer)
{
    return ogs_pool_cycle(&mme_bearer_pool, bearer);
}

void mme_session_remove_all(mme_ue_t *mme_ue)
{
    int i;

    mme_ue = mme_ue_cycle(mme_ue);
    ogs_assert(mme_ue);

    ogs_assert(mme_ue->num_of_session <= OGS_MAX_NUM_OF_SESS);
    for (i = 0; i < mme_ue->num_of_session; i++) {
        if (mme_ue->session[i].name) {
            ogs_free(mme_ue->session[i].name);
        }
    }

    mme_ue->num_of_session = 0;
}

ogs_session_t *mme_session_find_by_apn(mme_ue_t *mme_ue, char *apn)
{
    ogs_session_t *session = NULL;
    int i = 0;

    ogs_assert(mme_ue);
    ogs_assert(apn);

    ogs_assert(mme_ue->num_of_session <= OGS_MAX_NUM_OF_SESS);
    for (i = 0; i < mme_ue->num_of_session; i++) {
        session = &mme_ue->session[i];
        ogs_assert(session->name);
        if (ogs_strcasecmp(session->name, apn) == 0)
            return session;
    }

    return NULL;
}

ogs_session_t *mme_emergency_session(mme_ue_t *mme_ue)
{
    ogs_session_t *session = NULL;
    int i = 0;

    ogs_assert(mme_ue);
    ogs_assert(mme_ue->num_of_session <= OGS_MAX_NUM_OF_SESS);

    for (i = 0; i < mme_ue->num_of_session; i++) {
        session = &mme_ue->session[i];
        if (strstr(session->name, "sos")) {
            return session;
        }
    }

    return NULL;
}

ogs_session_t *mme_default_session(mme_ue_t *mme_ue)
{
    ogs_session_t *session = NULL;
    int i = 0;

    ogs_assert(mme_ue);

    ogs_assert(mme_ue->num_of_session <= OGS_MAX_NUM_OF_SESS);
    for (i = 0; i < mme_ue->num_of_session; i++) {
        session = &mme_ue->session[i];
        if (session->context_identifier == mme_ue->context_identifier)
            return session;
    }

    return NULL;
}

int mme_find_served_tai(ogs_eps_tai_t *tai)
{
    int i = 0, j = 0, k = 0;

    ogs_assert(tai);

    for (i = 0; i < self.num_of_served_tai; i++) {
        ogs_eps_tai0_list_t *list0 = &self.served_tai[i].list0;
        ogs_eps_tai1_list_t *list1 = &self.served_tai[i].list1;
        ogs_eps_tai2_list_t *list2 = &self.served_tai[i].list2;

        for (j = 0; list0->tai[j].num; j++) {
            ogs_assert(list0->tai[j].type == OGS_TAI0_TYPE);
            ogs_assert(list0->tai[j].num <= OGS_MAX_NUM_OF_TAI);

            for (k = 0; k < list0->tai[j].num; k++) {
                if (memcmp(&list0->tai[j].plmn_id,
                            &tai->plmn_id, OGS_PLMN_ID_LEN) == 0 &&
                    list0->tai[j].tac[k] == tai->tac) {
                    return i;
                }
            }
        }

        for (j = 0; list1->tai[j].num; j++) {
            ogs_assert(list1->tai[j].type == OGS_TAI1_TYPE);
            ogs_assert(list1->tai[j].num <= OGS_MAX_NUM_OF_TAI);

            if (list1->tai[j].tac <= tai->tac &&
                tai->tac < (list1->tai[j].tac+list1->tai[j].num))
                return i;
        }

        if (list2->num) {
            ogs_assert(list2->type == OGS_TAI2_TYPE);
            ogs_assert(list2->num <= OGS_MAX_NUM_OF_TAI);

            for (j = 0; j < list2->num; j++) {
                if (memcmp(&list2->tai[j].plmn_id,
                            &tai->plmn_id, OGS_PLMN_ID_LEN) == 0 &&
                    list2->tai[j].tac == tai->tac) {
                    return i;
                }
            }
        }
    }

    return -1;
}

#if 0 /* DEPRECATED */
int mme_m_tmsi_pool_generate(void)
{
    int j;
    int index = 0;

    ogs_trace("M-TMSI Pool try to generate...");
    while (index < ogs_app()->max.ue*2) {
        mme_m_tmsi_t *m_tmsi = NULL;
        int conflict = 0;

        m_tmsi = &m_tmsi_pool.array[index];
        ogs_assert(m_tmsi);
        *m_tmsi = ogs_random32();

        /* for mapped-GUTI */
        *m_tmsi |= 0xc0000000;
        *m_tmsi &= 0xff00ffff;

        for (j = 0; j < index; j++) {
            if (*m_tmsi == m_tmsi_pool.array[j]) {
                conflict = 1;
                ogs_trace("[M-TMSI CONFLICT]  %d:0x%x == %d:0x%x",
                        index, *m_tmsi, j, m_tmsi_pool.array[j]);
                break;
            }
        }
        if (conflict == 1) {
            continue;
        }

        index++;
    }
    m_tmsi_pool.size = index;
    ogs_trace("M-TMSI Pool generate...done");

    return OGS_OK;
}
#endif

mme_m_tmsi_t *mme_m_tmsi_alloc(void)
{
    mme_m_tmsi_t *m_tmsi = NULL;

    ogs_pool_alloc(&m_tmsi_pool, &m_tmsi);
    ogs_assert(m_tmsi);

    /* TS23.003
     * 2.8.2.1.2 Mapping in the UE
     *
     * E-UTRAN <M-TMSI> maps as follows:
     * - 6 bits of the E-UTRAN <M-TMSI> starting at bit 29 and down to bit 24
     * are mapped into bit 29 and down to bit 24 of the GERAN/UTRAN <P-TMSI>;
     * - 16 bits of the E-UTRAN <M-TMSI> starting at bit 15 and down to bit 0
     * are mapped into bit 15 and down to bit 0 of the GERAN/UTRAN <P-TMSI>;
     * - and the remaining 8 bits of the E-UTRAN <M-TMSI> are
     * mapped into the 8 Most Significant Bits of the <P-TMSI signature> field.
     *
     * The UE shall fill the remaining 2 octets of the <P-TMSI signature>
     * according to clauses 9.1.1, 9.4.1, 10.2.1, or 10.5.1
     * of 3GPP TS.33.401 [89] , as appropriate, for RAU/Attach procedures
     */

    ogs_assert(*m_tmsi <= 0x003fffff);

    *m_tmsi = ((*m_tmsi & 0xffff) | ((*m_tmsi & 0x003f0000) << 8));
    *m_tmsi |= 0xc0000000;

    return m_tmsi;
}

int mme_m_tmsi_free(mme_m_tmsi_t *m_tmsi)
{
    ogs_assert(m_tmsi);
    ogs_pool_free(&m_tmsi_pool, m_tmsi);

    return OGS_OK;
}

void mme_ebi_pool_init(mme_ue_t *mme_ue)
{
    int i, index;

    ogs_assert(mme_ue);

    ogs_pool_init(&mme_ue->ebi_pool, MAX_EPS_BEARER_ID-MIN_EPS_BEARER_ID+1);

    for (i = MIN_EPS_BEARER_ID, index = 0;
            i <= MAX_EPS_BEARER_ID; i++, index++) {
        mme_ue->ebi_pool.array[index] = i;
    }
}

void mme_ebi_pool_final(mme_ue_t *mme_ue)
{
    ogs_assert(mme_ue);

    ogs_pool_final(&mme_ue->ebi_pool);
}

void mme_ebi_pool_clear(mme_ue_t *mme_ue)
{
    ogs_assert(mme_ue);

    /* Suppress log message (mme_ue->ebi_pool.avail != mme_ue->ebi_pool.size) */
    mme_ue->ebi_pool.avail = mme_ue->ebi_pool.size;

    mme_ebi_pool_final(mme_ue);
    mme_ebi_pool_init(mme_ue);
}

uint8_t mme_selected_int_algorithm(mme_ue_t *mme_ue)
{
    int i;

    ogs_assert(mme_ue);

    for (i = 0; i < mme_self()->num_of_integrity_order; i++) {
        if (mme_ue->ue_network_capability.eia &
                (0x80 >> mme_self()->integrity_order[i])) {
            return mme_self()->integrity_order[i];
        }
    }

    return 0;
}

uint8_t mme_selected_enc_algorithm(mme_ue_t *mme_ue)
{
    int i;

    ogs_assert(mme_ue);

    for (i = 0; i < mme_self()->num_of_ciphering_order; i++) {
        if (mme_ue->ue_network_capability.eea &
                (0x80 >> mme_self()->ciphering_order[i])) {
            return mme_self()->ciphering_order[i];
        }
    }

    return 0;
}

static void stats_add_enb_ue(void)
{
    mme_metrics_inst_global_inc(MME_METR_GLOB_GAUGE_ENB_UE);
    num_of_enb_ue = num_of_enb_ue + 1;
    ogs_info("[Added] Number of eNB-UEs is now %d", num_of_enb_ue);
}

static void stats_remove_enb_ue(void)
{
    mme_metrics_inst_global_dec(MME_METR_GLOB_GAUGE_ENB_UE);
    num_of_enb_ue = num_of_enb_ue - 1;
    ogs_info("[Removed] Number of eNB-UEs is now %d", num_of_enb_ue);
}

static void stats_add_mme_session(void)
{
    mme_metrics_inst_global_inc(MME_METR_GLOB_GAUGE_MME_SESS);
    num_of_mme_sess = num_of_mme_sess + 1;
    ogs_info("[Added] Number of MME-Sessions is now %d", num_of_mme_sess);
}

static void stats_remove_mme_session(void)
{
    mme_metrics_inst_global_dec(MME_METR_GLOB_GAUGE_MME_SESS);
    num_of_mme_sess = num_of_mme_sess - 1;
    ogs_info("[Removed] Number of MME-Sessions is now %d", num_of_mme_sess);
}

static int rand_under(int val)
{
    if (val < 2) {
        return 0;
    }
    srand(time(NULL));
    return rand() % val;
}
