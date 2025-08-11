/*
 * Copyright (C) 2023 by Ryan Dimsey <ryan@omnitouch.com.au>
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
#include "naptr.h"
#include <arpa/inet.h>
#include <resolv.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include "ogs-dns-resolvers-logging.h"


enum { ORDER_SZ_BYTES = 2,
       PREFERENCE_SZ_BYTES = 2,
       FLAGS_LEN_SZ_BYTES = 1,
       SERVICE_LEN_SZ_BYTES = 1,
       REGEX_LEN_SZ_BYTES = 1 };

enum { MAX_ANSWER_BYTES = 4096 };


static naptr_resource_record * parse_naptr_resource_records(ns_msg * const handle, int count);
static bool parse_naptr_resource_record(const unsigned char * buf, uint16_t buf_sz, naptr_resource_record * const nrr);
static void get_regex_pattern_replace(char * const regex_str, char * const regex_pattern, size_t max_regex_pattern_sz, char * const regex_replace, size_t max_regex_replace_sz);
static inline int naptr_greater(naptr_resource_record *na, naptr_resource_record *nb);
static void _naptr_free_resource_record_list(naptr_resource_record * head);

// Helper: Convert linked list to array
naptr_resource_record ** naptr_list_to_array(naptr_resource_record *head, int *out_count) {
    if (!head || !out_count) return NULL;

    // Count nodes
    int count = 0;
    naptr_resource_record *cur = naptr_list_head(head);
    while (cur) {
        count++;
        cur = cur->next;
    }

    if (count == 0) {
        *out_count = 0;
        return NULL;
    }

    // Allocate array
    naptr_resource_record **array = malloc(sizeof(naptr_resource_record*) * count);
    if (!array) {
        *out_count = 0;
        return NULL;
    }

    // Fill array
    cur = naptr_list_head(head);
    int i;
    for (i = 0; i < count; i++) {
        array[i] = cur;
        cur = cur->next;
    }

    *out_count = count;
    return array;
}


naptr_resource_record * naptr_random_select(naptr_resource_record **array, int count) {
    if (!array || count <= 0) return NULL;

    static int seeded = 0;
    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = 1;
    }

    int best_order = array[0]->order;
    int same_order_count = 0;

    // Count how many have the same best order
        int i;
        for (i = 0; i < count; i++) {
        if (array[i]->order == best_order) {
            same_order_count++;
        } else {
            break; // sorted, so stop counting
        }
    }

    if (same_order_count == 0) return NULL;  // defensive, should not happen

    int idx = rand() % same_order_count;

    if (idx >= same_order_count || idx >= count) {
        ogs_debug("naptr_random_select: generated invalid index %d", idx);
        return NULL;
    }

    ogs_debug("Randomly selected NAPTR index: %d, replacement: %s", idx, array[idx]->replacement);
    return array[idx];
}


naptr_resource_record * naptr_query(const char* dname) {
    int count;
    int bytes_received;
    unsigned char answer[MAX_ANSWER_BYTES];
    ns_msg handle;
    naptr_resource_record *nrrs;

    if (NULL == dname) return NULL;

    /* Perform NAPTR lookup */
    /* NAPTR records serialised in buffer  */
    bytes_received = res_query(dname, ns_c_in, ns_t_naptr, answer, sizeof(answer));
    ogs_debug("[NAPTR-lookup] Query for '%s' resulted in %i bytes received\n", dname, bytes_received);
    if (bytes_received <= 0) {
        ogs_error("Query failed: '%s'", dname);
        return 0;
    }

    /* Parse response and process NAPTR records */
    /* NAPTR records in handler */
    ns_initparse(answer, bytes_received, &handle);
    count = ns_msg_count(handle, ns_s_an);

    /* NAPTR records in linked list */
    nrrs = parse_naptr_resource_records(&handle, count);

    if (NULL == nrrs) {
        ogs_error("Failed to parse NAPTR answers!\n");
        return 0;
    }

    return nrrs;
}

/*
 * Bubble sorts result record list according to naptr (order,preference).
 * Returns head to sorted list.
 */
naptr_resource_record * naptr_sort(naptr_resource_record **head) {
    int swapped;
    naptr_resource_record* current;
    naptr_resource_record* temp;

    if (*head == NULL) return NULL;

    do {
        swapped = 0;
        current = *head;

        while (current->next != NULL) {
            if (naptr_greater(current, current->next)) {
                if (current == *head) {
                    *head = current->next;
                    (*head)->prev = NULL;
                } else {
                    current->prev->next = current->next;
                    current->next->prev = current->prev;
                }

                temp = current->next->next;
                current->next->next = current;
                current->prev = current->next;
                current->next = temp;
                if (temp != NULL)
                    temp->prev = current;

                swapped = 1;
            } else {
                current = current->next;
            }
        }
    } while (swapped);

    return *head;
}

naptr_resource_record * naptr_list_head(naptr_resource_record * nrr) {
    if (NULL == nrr) return NULL;

    while (NULL != nrr->prev) {
        nrr = nrr->prev;
    }

    return nrr;
}

/*
 * Cases:
 *   1) If the parameter is NULL we return NULL.
 *   2) If we remove the head node we will return the new head node.
 *   3) If we remove the last node we will return the new last node.
 *   4) If we remove from a list with only 1 item we will return NULL.
 *   5) If we remove a node that is between two other we will return the
 *      removed nodes next node (the node at the index of the removed one)
 */
naptr_resource_record * naptr_remove_resource_record(naptr_resource_record * nrr) {
    if (NULL == nrr) return NULL;

    naptr_resource_record *res = NULL;
    naptr_resource_record *prev = nrr->prev;
    naptr_resource_record *next = nrr->next;

    if (NULL != prev) {
        prev->next = next;
        res = prev;
    }

    if (NULL != next) {
        next->prev = prev;
        res = next;
    }

    free(nrr);

    return res;
}

void naptr_free_resource_record_list(naptr_resource_record * nrr) {
    if (NULL != nrr) {
        /* Make sure we start at the head */
        nrr = naptr_list_head(nrr);
        _naptr_free_resource_record_list(nrr);
    }
}

int naptr_resource_record_list_count(naptr_resource_record * nrr) {
    int count = 0;

    if (NULL == nrr) return 0;

    nrr = naptr_list_head(nrr);
    while (NULL != nrr) {
        ++count;
        nrr = nrr->next;
    }

    return count;
}

/* Returns the head of the doubly linked list */
static naptr_resource_record * parse_naptr_resource_records(ns_msg * const handle, int count) {
    ns_rr rr;
    int i = 0;
    naptr_resource_record * nrr_current = NULL;
    naptr_resource_record * nrr_next = NULL;

    if ((NULL == handle) || (0 == count)) return NULL;

    for (i = 0; i < count; i++) {
        if (0 != ns_parserr(handle, ns_s_an, i, &rr)) {
            ogs_error("Failed to parse NAPTR Resource Record... skipping...");
            continue;
        }

        if (ns_rr_type(rr) == ns_t_naptr) {
            /* Make memory for new nrr */
            nrr_current = (naptr_resource_record*)malloc(sizeof(naptr_resource_record));
            if (NULL == nrr_current) {
                ogs_fatal("Critical failure... Could not allocate memory\n");
                exit(-1);
            }
            memset(nrr_current, 0, sizeof(naptr_resource_record));

            /* Set the current NRRs data */
            parse_naptr_resource_record(&rr.rdata[0], rr.rdlength, nrr_current);

            /* This NRR will be added to the start of the list,
             * meaning that the next NRR will be the one we created
             * in the previous iteration. */
            if (NULL != nrr_next) {
                nrr_current->next = nrr_next;
                nrr_next->prev = nrr_current;
            }

            /* The previous NRR doesn't exist yet */
            nrr_current->prev = NULL;

            /* Now this NRR will be the next for the NRR created in the
             * following iteration */
            nrr_next = nrr_current;
        }
    }

    return naptr_list_head(nrr_current);
}

static bool parse_naptr_resource_record(const unsigned char * buf, uint16_t buf_sz, naptr_resource_record * const nrr) {
    bool success = false;

    if ((0 == buf) || (0 == nrr)) return false;

    /* Num of '!' chars is 3 */
    enum { MAX_REGEX_STR = MAX_REGEX_PATTERN_STR + MAX_REGEX_REPLACE_STR + 3 };
    int flags_len;
    int service_len;
    int regex_len;
    size_t bytes_consumed = 0;
    char regex[MAX_REGEX_STR] = "";

    nrr->order = ns_get16(&buf[bytes_consumed]);
    bytes_consumed += ORDER_SZ_BYTES;

    nrr->preference = ns_get16(&buf[bytes_consumed]);
    bytes_consumed += PREFERENCE_SZ_BYTES;

    flags_len = buf[bytes_consumed];
    bytes_consumed += FLAGS_LEN_SZ_BYTES;

    /* Assuming that the flag(s) will only be either 'a' or 's' */
    nrr->flag = buf[bytes_consumed];
    bytes_consumed += flags_len;

    service_len = buf[bytes_consumed];
    bytes_consumed += SERVICE_LEN_SZ_BYTES;

    memcpy(nrr->service, &buf[bytes_consumed], service_len);
    bytes_consumed += service_len;

    regex_len = buf[bytes_consumed];
    bytes_consumed += REGEX_LEN_SZ_BYTES;

    memcpy(regex, &buf[bytes_consumed], regex_len);
    bytes_consumed += regex_len;

    int bytes_uncompressed = ns_name_uncompress(
        &buf[0],              /* Start of compressed buffer */
        &buf[buf_sz],         /* End of compressed buffer */
        &buf[bytes_consumed], /* Where to start decompressing */
        nrr->replacement,     /* Where to store decompressed value */
        MAX_REPLACEMENT_STR   /* Number of bytes that can be stored in output buffer */
    );

    get_regex_pattern_replace(regex, nrr->regex_pattern, MAX_REGEX_PATTERN_STR, nrr->regex_replace, MAX_REGEX_REPLACE_STR);

    if (0 <= bytes_uncompressed) {
        success = true;
    }

    return success;
}

/* Note: This will mutate regex_str */
static void get_regex_pattern_replace(char * const regex_str, char * const regex_pattern, size_t max_regex_pattern_sz, char * const regex_replace, size_t max_regex_replace_sz) {
    if ((NULL == regex_str)     ||
        (NULL == regex_pattern) ||
        (NULL == regex_replace)) {
        return;
    }
    
    char* regex_pattern_ptr = strtok(regex_str, "!");
    if (NULL == regex_pattern_ptr) return;

    char* regex_replace_ptr = strtok(NULL, "!");
    if (NULL == regex_replace_ptr) return;

    strncpy(regex_pattern, regex_pattern_ptr, max_regex_pattern_sz - 1);
    strncpy(regex_replace, regex_replace_ptr, max_regex_replace_sz - 1);
}

/*
 * Tests if one result record is "greater" that the other.  Non-NAPTR records
 * greater that NAPTR record.  An invalid NAPTR record is greater than a 
 * valid one.  Valid NAPTR records are compared based on their
 * (order,preference).
 
*/

static inline int naptr_greater(naptr_resource_record *na, naptr_resource_record *nb) {
    if (NULL == na) return 1;
    if (NULL == nb) return 0;

    if (na->order > nb->order) {
        return 1;
    } else if (na->order == nb->order) {
        return na->preference > nb->preference;
    }

    return 0;
}


static void _naptr_free_resource_record_list(naptr_resource_record * head) {
    if (NULL != head) {
        /* Use the 'head' to free the 'tail' */
        _naptr_free_resource_record_list(head->next);
        
        /* Free the 'head' */
        free(head);
    }
}
