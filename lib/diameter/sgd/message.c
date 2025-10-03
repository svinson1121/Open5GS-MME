/*
 * Copyright (C) 2019 by Sukchan Lee <acetcom@gmail.com>
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

#include "ogs-diameter-sgd.h"

#define CHECK_dict_search( _type, _criteria, _what, _result )    \
    CHECK_FCT(  fd_dict_search( fd_g_config->cnf_dict, (_type), (_criteria), (_what), (_result), ENOENT) );

int ogs_diam_sgd_init(void)
{
    application_id_t id = OGS_DIAM_SGD_APPLICATION_ID;

    ogs_assert(ogs_dict_sgd_entry(NULL) == 0);

    CHECK_dict_search(DICT_APPLICATION, APPLICATION_BY_ID, (void *)&id, &ogs_diam_sgd_application);

    CHECK_dict_search(DICT_COMMAND, CMD_BY_NAME, "MO-Forward-Short-Message-Request", &ogs_diam_sgd_cmd_ofr);
    CHECK_dict_search(DICT_COMMAND, CMD_BY_NAME, "MO-Forward-Short-Message-Answer", &ogs_diam_sgd_cmd_ofa);
    CHECK_dict_search(DICT_COMMAND, CMD_BY_NAME, "MT-Forward-Short-Message-Request", &ogs_diam_sgd_cmd_tfr);
    CHECK_dict_search(DICT_COMMAND, CMD_BY_NAME, "MT-Forward-Short-Message-Answer", &ogs_diam_sgd_cmd_tfa);
    CHECK_dict_search(DICT_COMMAND, CMD_BY_NAME, "Alert-Service-Centre-Request", &ogs_diam_sgd_cmd_alr);
    CHECK_dict_search(DICT_COMMAND, CMD_BY_NAME, "Alert-Service-Centre-Answer", &ogs_diam_sgd_cmd_ala);

    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "SC-Address", &ogs_diam_sgd_sc_address);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "SM-RP-UI", &ogs_diam_sgd_sm_rp_ui);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "TFR-Flags", &ogs_diam_sgd_tfr_flags);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "SM-Delivery-Failure-Cause", &ogs_diam_sgd_sm_delivery_failure_cause);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "SM-Diagnostic-Info", &ogs_diam_sgd_sm_diagnostic_info);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "SM-Delivery-Timer", &ogs_diam_sgd_sm_delivery_timer);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "SM-Delivery-Start-Time", &ogs_diam_sgd_sm_delivery_start_time);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "SMS-MI-Correlation-ID", &ogs_diam_sgd_mi_correlation_id);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "HSS-ID", &ogs_diam_sgd_hss_id);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "Originating-SIP-URI", &ogs_diam_sgd_originating_sip_uri);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "Destination-SIP-URI", &ogs_diam_sgd_destination_sip_uri);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "OFR-Flags", &ogs_diam_sgd_ofr_flags);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "Maximum-Retransmission-Time", &ogs_diam_sgd_maximum_retransmission_time);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "Requested-Retransmission-Time", &ogs_diam_sgd_requested_retransmission_time);
    CHECK_dict_search(DICT_AVP, AVP_BY_NAME_ALL_VENDORS, "SMS-GMSC-Address", &ogs_diam_sgd_sms_gmsc_address);

    return 0;
}
