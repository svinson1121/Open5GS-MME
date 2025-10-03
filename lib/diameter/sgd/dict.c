/*
 * Software License Agreement (BSD License)
 * Author: Thomas Klausner <tk@giga.or.at>
 *
 * Copyright (c) 2013, Thomas Klausner
 * All rights reserved.
 *
 * Written under contract by nfotex IT GmbH, http://nfotex.com/
 *
 * Redistribution and use of this software in source and binary forms, with or without modification, are
 * permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer in the documentation and/or other
 *   materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <freeDiameter/extension.h>
#include "ogs-diameter-sgd.h"

/* Define global dictionary objects */
struct dict_object *ogs_diam_sgd_application;
struct dict_object *ogs_diam_sgd_cmd_ofr;
struct dict_object *ogs_diam_sgd_cmd_ofa;
struct dict_object *ogs_diam_sgd_cmd_tfr;
struct dict_object *ogs_diam_sgd_cmd_tfa;
struct dict_object *ogs_diam_sgd_cmd_alr;
struct dict_object *ogs_diam_sgd_cmd_ala;
struct dict_object *ogs_diam_sgd_sc_address;
struct dict_object *ogs_diam_sgd_sm_rp_ui;
struct dict_object *ogs_diam_sgd_tfr_flags;
struct dict_object *ogs_diam_sgd_sm_delivery_failure_cause;
struct dict_object *ogs_diam_sgd_sm_diagnostic_info;
struct dict_object *ogs_diam_sgd_sm_delivery_timer;
struct dict_object *ogs_diam_sgd_sm_delivery_start_time;
struct dict_object *ogs_diam_sgd_mi_correlation_id;
struct dict_object *ogs_diam_sgd_hss_id;
struct dict_object *ogs_diam_sgd_originating_sip_uri;
struct dict_object *ogs_diam_sgd_destination_sip_uri;
struct dict_object *ogs_diam_sgd_ofr_flags;
struct dict_object *ogs_diam_sgd_maximum_retransmission_time;
struct dict_object *ogs_diam_sgd_requested_retransmission_time;
struct dict_object *ogs_diam_sgd_sms_gmsc_address;

#define CHECK_dict_new( _type, _data, _parent, _ref )  \
  CHECK_FCT(  fd_dict_new( fd_g_config->cnf_dict, (_type), (_data), (_parent), (_ref))  );

#define CHECK_dict_search( _type, _criteria, _what, _result )  \
  CHECK_FCT(  fd_dict_search( fd_g_config->cnf_dict, (_type), (_criteria), (_what), (_result), ENOENT) );

struct local_rules_definition {
  struct dict_avp_request avp_vendor_plus_name;
  enum rule_position  position;
  int       min;
  int      max;
};

#define RULE_ORDER( _position ) ((((_position) == RULE_FIXED_HEAD) || ((_position) == RULE_FIXED_TAIL)) ? 1 : 0 )

#define PARSE_loc_rules( _rulearray, _parent) {                \
  int __ar;                      \
  for (__ar=0; __ar < sizeof(_rulearray) / sizeof((_rulearray)[0]); __ar++) {      \
    struct dict_rule_data __data = { NULL,               \
      (_rulearray)[__ar].position,              \
      0,                     \
      (_rulearray)[__ar].min,                \
      (_rulearray)[__ar].max};              \
    __data.rule_order = RULE_ORDER(__data.rule_position);          \
    CHECK_FCT(  fd_dict_search(                 \
      fd_g_config->cnf_dict,                \
      DICT_AVP,                   \
      AVP_BY_NAME_AND_VENDOR,               \
      &(_rulearray)[__ar].avp_vendor_plus_name,          \
      &__data.rule_avp, 0 ) );              \
    if ( !__data.rule_avp ) {                \
      TRACE_DEBUG(INFO, "AVP Not found: '%s'", (_rulearray)[__ar].avp_vendor_plus_name.avp_name);    \
      return ENOENT;                  \
    }                      \
    CHECK_FCT_DO( fd_dict_new( fd_g_config->cnf_dict, DICT_RULE, &__data, _parent, NULL),  \
      {                          \
        TRACE_DEBUG(INFO, "Error on rule with AVP '%s'",            \
              (_rulearray)[__ar].avp_vendor_plus_name.avp_name);    \
        return EINVAL;                      \
      } );                          \
  }                              \
}

int ogs_dict_sgd_entry(char *conffile)
{
  /* Applications section */
  {
    struct dict_object *vendor;
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_VENDOR, VENDOR_BY_NAME, "3GPP", &vendor, ENOENT));
    struct dict_application_data sgd = { 16777313, "SGd" };
    CHECK_FCT(fd_dict_new(fd_g_config->cnf_dict, DICT_APPLICATION, &sgd, vendor, &ogs_diam_sgd_application));
  }

  /* Commands section */
  {
    struct dict_object *sgd;
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_APPLICATION, APPLICATION_BY_NAME, "SGd", &sgd, ENOENT));

    /* MO-Forward-Short-Message-Request (OFR) Command */
    {
      struct dict_cmd_data data = {
        8388620,                                                /* Code */
        "MO-Forward-Short-Message-Request",                     /* Name */
        CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE | CMD_FLAG_ERROR, /* Fixed flags */
        CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE                   /* Fixed flag values */
      };
      struct local_rules_definition rules[] =
      {
        {  {                      .avp_name = "Session-Id" }, RULE_FIXED_HEAD, -1, 1 },
        {  {                      .avp_name = "Vendor-Specific-Application-Id" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Auth-Session-State" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Host" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Realm" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Destination-Host" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Destination-Realm" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "User-Name" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "Supported-Features" }, RULE_OPTIONAL, -1, -1 },
        {  { .avp_vendor = 10415, .avp_name = "SC-Address" }, RULE_REQUIRED, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SM-RP-UI" }, RULE_REQUIRED, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "OFR-Flags" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SMS-GMSC-Address" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SMS-MI-Correlation-ID" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Proxy-Info" }, RULE_OPTIONAL, -1, -1 },
        {  {                      .avp_name = "Route-Record" }, RULE_OPTIONAL, -1, -1 }
      };

      CHECK_dict_new(DICT_COMMAND, &data, sgd, &ogs_diam_sgd_cmd_ofr);
      PARSE_loc_rules(rules, ogs_diam_sgd_cmd_ofr);
    }

    /* MO-Forward-Short-Message-Answer (OFA) Command */
    {
      struct dict_cmd_data data = {
        8388620,                                                /* Code */
        "MO-Forward-Short-Message-Answer",                      /* Name */
        CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE | CMD_FLAG_ERROR, /* Fixed flags */
        CMD_FLAG_PROXIABLE                                      /* Fixed flag values */
      };
      struct local_rules_definition rules[] =
      {
        {  {                      .avp_name = "Session-Id" }, RULE_FIXED_HEAD, -1, 1 },
        {  {                      .avp_name = "Vendor-Specific-Application-Id" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Result-Code" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Experimental-Result" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Auth-Session-State" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Host" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Realm" }, RULE_REQUIRED, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "Supported-Features" }, RULE_OPTIONAL, -1, -1 },
        {  { .avp_vendor = 10415, .avp_name = "SM-RP-UI" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SMS-MI-Correlation-ID" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Failed-AVP" }, RULE_OPTIONAL, -1, -1 },
        {  {                      .avp_name = "Proxy-Info" }, RULE_OPTIONAL, -1, -1 },
        {  {                      .avp_name = "Route-Record" }, RULE_OPTIONAL, -1, -1 }
      };

      CHECK_dict_new(DICT_COMMAND, &data, sgd, &ogs_diam_sgd_cmd_ofa);
      PARSE_loc_rules(rules, ogs_diam_sgd_cmd_ofa);
    }

    /* MT-Forward-Short-Message-Request (TFR) Command */
    {
      struct dict_cmd_data data = {
        8388621,                                                /* Code */
        "MT-Forward-Short-Message-Request",                     /* Name */
        CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE | CMD_FLAG_ERROR, /* Fixed flags */
        CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE                   /* Fixed flag values */
      };
      struct local_rules_definition rules[] =
      {
        {  {                      .avp_name = "Session-Id" }, RULE_FIXED_HEAD, -1, 1 },
        {  {                      .avp_name = "Vendor-Specific-Application-Id" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Auth-Session-State" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Host" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Realm" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Destination-Host" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Destination-Realm" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "User-Name" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "Supported-Features" }, RULE_OPTIONAL, -1, -1 },
        {  { .avp_vendor = 10415, .avp_name = "SC-Address" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SM-RP-UI" }, RULE_REQUIRED, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "TFR-Flags" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SMS-MI-Correlation-ID" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "Maximum-Retransmission-Time" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "Requested-Retransmission-Time" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SMS-GMSC-Address" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "HSS-ID" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "Originating-SIP-URI" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "Destination-SIP-URI" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Proxy-Info" }, RULE_OPTIONAL, -1, -1 },
        {  {                      .avp_name = "Route-Record" }, RULE_OPTIONAL, -1, -1 }
      };

      CHECK_dict_new(DICT_COMMAND, &data, sgd, &ogs_diam_sgd_cmd_tfr);
      PARSE_loc_rules(rules, ogs_diam_sgd_cmd_tfr);
    }

    /* MT-Forward-Short-Message-Answer (TFA) Command */
    {
      struct dict_cmd_data data = {
        8388621,                                                /* Code */
        "MT-Forward-Short-Message-Answer",                      /* Name */
        CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE | CMD_FLAG_ERROR, /* Fixed flags */
        CMD_FLAG_PROXIABLE                                      /* Fixed flag values */
      };
      struct local_rules_definition rules[] =
      {
        {  {                      .avp_name = "Session-Id" }, RULE_FIXED_HEAD, -1, 1 },
        {  {                      .avp_name = "Vendor-Specific-Application-Id" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Result-Code" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Experimental-Result" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Auth-Session-State" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Host" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Realm" }, RULE_REQUIRED, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "Supported-Features" }, RULE_OPTIONAL, -1, -1 },
        {  { .avp_vendor = 10415, .avp_name = "SM-RP-UI" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SM-Delivery-Failure-Cause" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SM-Diagnostic-Info" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SMS-MI-Correlation-ID" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Failed-AVP" }, RULE_OPTIONAL, -1, -1 },
        {  {                      .avp_name = "Proxy-Info" }, RULE_OPTIONAL, -1, -1 },
        {  {                      .avp_name = "Route-Record" }, RULE_OPTIONAL, -1, -1 }
      };

      CHECK_dict_new(DICT_COMMAND, &data, sgd, &ogs_diam_sgd_cmd_tfa);
      PARSE_loc_rules(rules, ogs_diam_sgd_cmd_tfa);
    }

    /* Alert-Service-Centre-Request (ALR) Command */
    {
      struct dict_cmd_data data = {
        8388622,                                                /* Code */
        "Alert-Service-Centre-Request",                         /* Name */
        CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE | CMD_FLAG_ERROR, /* Fixed flags */
        CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE                   /* Fixed flag values */
      };
      struct local_rules_definition rules[] =
      {
        {  {                      .avp_name = "Session-Id" }, RULE_FIXED_HEAD, -1, 1 },
        {  {                      .avp_name = "Vendor-Specific-Application-Id" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Auth-Session-State" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Host" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Realm" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Destination-Host" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Destination-Realm" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "User-Name" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "Supported-Features" }, RULE_OPTIONAL, -1, -1 },
        {  { .avp_vendor = 10415, .avp_name = "SC-Address" }, RULE_REQUIRED, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SM-Delivery-Start-Time" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SM-Delivery-Timer" }, RULE_OPTIONAL, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "SMS-GMSC-Address" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Proxy-Info" }, RULE_OPTIONAL, -1, -1 },
        {  {                      .avp_name = "Route-Record" }, RULE_OPTIONAL, -1, -1 }
      };

      CHECK_dict_new(DICT_COMMAND, &data, sgd, &ogs_diam_sgd_cmd_alr);
      PARSE_loc_rules(rules, ogs_diam_sgd_cmd_alr);
    }

    /* Alert-Service-Centre-Answer (ALA) Command */
    {
      struct dict_cmd_data data = {
        8388622,                                                /* Code */
        "Alert-Service-Centre-Answer",                          /* Name */
        CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE | CMD_FLAG_ERROR, /* Fixed flags */
        CMD_FLAG_PROXIABLE                                      /* Fixed flag values */
      };
      struct local_rules_definition rules[] =
      {
        {  {                      .avp_name = "Session-Id" }, RULE_FIXED_HEAD, -1, 1 },
        {  {                      .avp_name = "Vendor-Specific-Application-Id" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Result-Code" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Experimental-Result" }, RULE_OPTIONAL, -1, 1 },
        {  {                      .avp_name = "Auth-Session-State" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Host" }, RULE_REQUIRED, -1, 1 },
        {  {                      .avp_name = "Origin-Realm" }, RULE_REQUIRED, -1, 1 },
        {  { .avp_vendor = 10415, .avp_name = "Supported-Features" }, RULE_OPTIONAL, -1, -1 },
        {  {                      .avp_name = "Failed-AVP" }, RULE_OPTIONAL, -1, -1 },
        {  {                      .avp_name = "Proxy-Info" }, RULE_OPTIONAL, -1, -1 },
        {  {                      .avp_name = "Route-Record" }, RULE_OPTIONAL, -1, -1 }
      };

      CHECK_dict_new(DICT_COMMAND, &data, sgd, &ogs_diam_sgd_cmd_ala);
      PARSE_loc_rules(rules, ogs_diam_sgd_cmd_ala);
    }
  }

  /* Initialize AVP objects */
  {
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
  }

  LOG_D("Extension 'Dictionary definitions for SGd' initialized");
  return 0;
}
