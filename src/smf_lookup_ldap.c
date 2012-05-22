/* spmfilter - mail filtering framework
 * Copyright (C) 2009-2010 Axel Steiner and SpaceNet AG
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <lber.h>
#include <ldap.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>



#include "smf_trace.h"
#include "smf_settings.h"
#include "smf_lookup.h"
#include "smf_lookup_private.h"
#include "smf_core.h"
#include "smf_session.h"

#define THIS_MODULE "ldap_lookup"
LDAP *ld = NULL;



int ldap_get_scope(char *ldap_scope) {
    if (g_ascii_strcasecmp(ldap_scope,"subtree") == 0)
        return LDAP_SCOPE_SUBTREE;
    else if (g_ascii_strcasecmp(ldap_scope,"onelevel") == 0)
        return LDAP_SCOPE_ONELEVEL;
    else if (g_ascii_strcasecmp(ldap_scope,"base") == 0)
        return LDAP_SCOPE_BASE;
    else
        return LDAP_SCOPE_SUBTREE;
}


char *ldap_get_uri(char *ldap_host, int ldap_port) {
    char *uri;
    uri = g_strdup_printf("ldap://%s:%d",ldap_host,ldap_port);
    return uri;
}


int smf_lookup_ldap_connect(char *ldap_uri, SMFSettings_T *settings)  {
    
    int ret;
    int version;
    char *uri;
    char *host;

    if (*ldap_uri) {
        if ((ret = ldap_initialize(&ld, ldap_uri) != LDAP_SUCCESS)) 
            TRERR("ldap_initialize() returned [%d]", ret);
    } else {
        if (g_ascii_strcasecmp(settings->backend_connection,"balance") == 0) {
            host = ldap_get_rand_host(settings);
        } else
            host = settings->ldap_host[0];
            uri = ldap_get_uri(host, 389);
        if ((ret = ldap_initialize(&ld, uri)) != LDAP_SUCCESS) {
            TRERR("ldap_initialize() returned [%d]", ret);
        }
    }

    version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    if (settings->ldap_referrals) {
        ldap_set_option(ld, LDAP_OPT_REFERRALS, (void *)LDAP_OPT_ON);
        TRACE(TRACE_LOOKUP, "set ldap referrals to on");
    } else {
        ldap_set_option(ld, LDAP_OPT_REFERRALS, (void *)LDAP_OPT_OFF);
        TRACE(TRACE_LOOKUP, "set ldap referrals to off");
    }

    if (smf_ldap_bind(ldap_uri, settings) != 0) {
        if (ldap_failover_connect(settings) != 0) {
            TRERR("failover connection failed");
            return -1;
        }
    }

    return 0;
}

char *ldap_get_rand_host(SMFSettings_T *settings) {
    TRACE(TRACE_DEBUG,"trying to get random ldap server");
    srand(time(NULL));
    return settings->ldap_host[rand() % settings->ldap_num_hosts];
}

int smf_ldap_bind(char *uri, SMFSettings_T *settings) {
    int ret, err;
    struct berval *cred;

    cred = malloc(sizeof(struct berval));
    cred->bv_len = strlen(settings->ldap_bindpw);
    cred->bv_val = settings->ldap_bindpw;

    if ((ret = ldap_initialize(&ld, uri)) == LDAP_SUCCESS) {
        printf("ldap_initialize() to host [%s] successful ", uri);
        TRACE(TRACE_LOOKUP, "binding to ldap server as [%s] / [xxxxxxxx]",  settings->ldap_binddn);

        if ((err = ldap_sasl_bind_s(ld,settings->ldap_binddn,LDAP_SASL_SIMPLE,cred,NULL,NULL,NULL))) {
            TRACE(TRACE_ERR, "ldap_sasl_bind_s on host [%s] failed: %s", uri, ldap_err2string(err));
            printf("ldap_sasl_bind_s on host [%s] failed: %s", uri, ldap_err2string(err));
            return -1;
        }

        TRACE(TRACE_LOOKUP, "successfully bound to host [%s]", uri);
        printf("successfully bound to host [%s]", uri, ldap_err2string(err));
        
        free(cred);
        g_free(uri);
        return 0;
    } 

    free(cred);
    g_free(uri);
    return -1;
}

int ldap_failover_connect(SMFSettings_T *settings) {
    int i;
    char *uri;
    
    for (i=0; i < settings->ldap_num_hosts; i++) {
        uri = ldap_get_uri(settings->ldap_host[i], 389);
        if (smf_ldap_bind(uri, settings) != 0) {
            continue;
        } else {
            return 0;
        }
    }

    return -1;
}



/** Connect to LDAP server
 *
 * \returns 0 on success or -1 in case of error
 */

 /*
int smf_lookup_ldap_connect(char *ldap_uri) {
    int ret;
    int version;
    char *uri;
    char *host;
    

    //SMFSettings_T *settings = smf_settings_get();

    if (settings->ldap_uri) {
        if ((ret = ldap_initialize(&ld, settings->ldap_uri) != LDAP_SUCCESS)) 
            TRACE(TRACE_ERR, "ldap_initialize() returned [%d]", ret);
    } else {
        if (g_ascii_strcasecmp(settings->backend_connection,"balance") == 0) {
            host = ldap_get_rand_host();
        } else
            host = settings->ldap_host[0];
        
        uri = ldap_get_uri(host);
        if ((ret = ldap_initialize(&ld, uri)) != LDAP_SUCCESS) {
            TRACE(TRACE_ERR, "ldap_initialize() returned [%d]", ret);
        }
    }
    version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    if (settings->ldap_referrals) {
        ldap_set_option(ld, LDAP_OPT_REFERRALS, (void *)LDAP_OPT_ON);
        TRACE(TRACE_LOOKUP, "set ldap referrals to on");
    } else {
        ldap_set_option(ld, LDAP_OPT_REFERRALS, (void *)LDAP_OPT_OFF);
        TRACE(TRACE_LOOKUP, "set ldap referrals to off");
    }

    if (smf_ldap_bind(uri) != 0) {
        if (ldap_failover_connect() != 0) {
            TRACE(TRACE_ERR,"failover connection failed");
            return -1;
        }
    }

    return 0;
}

*/

/** Get active LDAP connection, if no connection
 *  is available reconnect to LDAP server.
 *
 * \returns pointer to LDAP connection
 */

 /*
LDAP *ldap_con_get(void) {
    if (!ld) {
        smf_lookup_ldap_connect();
    }
    return ld;
}
*/

/** Disconnect from LDAP server */
/*
void smf_lookup_ldap_disconnect(void) {
    LDAP *c = ldap_con_get();
    if (c != NULL) {
        ldap_unbind_ext_s(c,NULL,NULL);
        TRACE(TRACE_LOOKUP, "unbind ldap server");
    }
}

SMFLookupResult_T *smf_lookup_ldap_query(const char *q, ...) {
    va_list ap, cp;
    char *query;
    LDAPMessage *msg = NULL;
    LDAPMessage *entry = NULL;
    char *attr;
    struct berval **bvals;
    BerElement *ptr;
    int i,value_count;
    LDAP *c = ldap_con_get();

    SMFLookupResult_T *result = smf_lookup_result_new();
    //SMFSettings_T *settings = smf_settings_get();

    va_start(ap, q);
    va_copy(cp, ap);
    query = g_strdup_vprintf(q, cp);
    va_end(cp);
    g_strstrip(query);

    if (strlen(query) == 0)
        return NULL;

    TRACE(TRACE_LOOKUP,"[%p] [%s]",c,query);
    
    if (ldap_search_ext_s(c,settings->ldap_base,get_scope(),query,NULL,0,NULL, NULL, NULL, 0, &msg) != LDAP_SUCCESS)
        TRACE(TRACE_ERR,"[%p] query [%s] failed",ld, query);
    
    if(ldap_count_entries(c,msg) <= 0) {
        TRACE(TRACE_LOOKUP,"[%p] nothing found",c);
        g_free(query);
        return NULL;
    } else
        TRACE(TRACE_LOOKUP,"[%p] found [%d] entries", c, ldap_count_entries(c,msg));

    for (entry = ldap_first_entry(c, msg); entry != NULL; entry = ldap_next_entry(c,entry)) {
        SMFLookupElement_T *e = smf_lookup_element_new();

        for(attr = ldap_first_attribute(c, msg, &ptr); attr != NULL;
                attr = ldap_next_attribute(c, msg, ptr)) {

            SMFLdapValue_T *vals = malloc(sizeof(SMFLdapValue_T));
            bvals = ldap_get_values_len(c, entry, attr);
            value_count = ldap_count_values_len(bvals);
            TRACE(TRACE_LOOKUP,"found attribute [%s] in entry [%p] with [%d] values", attr, entry, value_count);

            vals->len = value_count;
            vals->data = (char **)calloc(value_count, sizeof(char *));
            for (i = 0; i < value_count; i++) {
                vals->data[i] = (char *)malloc(strlen((char *)((struct berval)*bvals[i]).bv_val) + 1);
                strcpy(vals->data[i], (char *)((struct berval)*bvals[i]).bv_val);
            }
            smf_lookup_element_add(e,g_strdup(attr),vals);
            ldap_value_free_len(bvals);
            free(attr);
        }
        smf_lookup_result_add(result,e);
    }

    ber_free(ptr,0);
    ldap_msgfree(msg);
    g_free(query);
    return result;
}
*/

/** Check if given user exists in ldap directory
 *
 * \param user a SMFEmailAddress_T object
 */
 /*
void smf_lookup_ldap_check_user(SMFEmailAddress_T *user) {
    SMFSettings_T *settings = smf_settings_get();
    char *query;

    smf_core_expand_string(settings->ldap_user_query,user->addr,&query);
    user->user_data = NULL;
    user->user_data = smf_lookup_ldap_query(query);
    free(query);
}
*/
