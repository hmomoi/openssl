/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* ====================================================================
 * Copyright 2005 Nokia. All rights reserved.
 *
 * The portions of the attached software ("Contribution") is developed by
 * Nokia Corporation and is licensed pursuant to the OpenSSL open source
 * license.
 *
 * The Contribution, originally written by Mika Kousa and Pasi Eronen of
 * Nokia Corporation, consists of the "PSK" (Pre-Shared Key) ciphersuites
 * support (see RFC 4279) to OpenSSL.
 *
 * No patent licenses or other rights except those expressly stated in
 * the OpenSSL open source license shall be deemed granted or received
 * expressly, by implication, estoppel, or otherwise.
 *
 * No assurances are provided by Nokia that the Contribution does not
 * infringe the patent or other intellectual property rights of any third
 * party or that the license provides you with all the necessary rights
 * to make use of the Contribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. IN
 * ADDITION TO THE DISCLAIMERS INCLUDED IN THE LICENSE, NOKIA
 * SPECIFICALLY DISCLAIMS ANY LIABILITY FOR CLAIMS BROUGHT BY YOU OR ANY
 * OTHER ENTITY BASED ON INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS OR
 * OTHERWISE.
 */

#include <stdio.h>
#include <stdlib.h>
#include "ssl_locl.h"
#include <openssl/asn1t.h>
#include <openssl/x509.h>

typedef struct {
    long version;
    long ssl_version;
    ASN1_OCTET_STRING *cipher;
    ASN1_OCTET_STRING *comp_id;
    ASN1_OCTET_STRING *master_key;
    ASN1_OCTET_STRING *session_id;
    ASN1_OCTET_STRING *key_arg;
    long time;
    long timeout;
    X509 *peer;
    ASN1_OCTET_STRING *session_id_context;
    long verify_result;
    ASN1_OCTET_STRING *tlsext_hostname;
    long tlsext_tick_lifetime_hint;
    long tlsext_tick_age_add;
    ASN1_OCTET_STRING *tlsext_tick;
#ifndef OPENSSL_NO_PSK
    ASN1_OCTET_STRING *psk_identity_hint;
    ASN1_OCTET_STRING *psk_identity;
#endif
#ifndef OPENSSL_NO_SRP
    ASN1_OCTET_STRING *srp_username;
#endif
    long flags;
    uint32_t max_early_data;
    ASN1_OCTET_STRING *alpn_selected;
} SSL_SESSION_ASN1;

ASN1_SEQUENCE(SSL_SESSION_ASN1) = {
    ASN1_SIMPLE(SSL_SESSION_ASN1, version, LONG),
    ASN1_SIMPLE(SSL_SESSION_ASN1, ssl_version, LONG),
    ASN1_SIMPLE(SSL_SESSION_ASN1, cipher, ASN1_OCTET_STRING),
    ASN1_SIMPLE(SSL_SESSION_ASN1, session_id, ASN1_OCTET_STRING),
    ASN1_SIMPLE(SSL_SESSION_ASN1, master_key, ASN1_OCTET_STRING),
    ASN1_IMP_OPT(SSL_SESSION_ASN1, key_arg, ASN1_OCTET_STRING, 0),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, time, ZLONG, 1),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, timeout, ZLONG, 2),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, peer, X509, 3),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, session_id_context, ASN1_OCTET_STRING, 4),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, verify_result, ZLONG, 5),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, tlsext_hostname, ASN1_OCTET_STRING, 6),
#ifndef OPENSSL_NO_PSK
    ASN1_EXP_OPT(SSL_SESSION_ASN1, psk_identity_hint, ASN1_OCTET_STRING, 7),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, psk_identity, ASN1_OCTET_STRING, 8),
#endif
    ASN1_EXP_OPT(SSL_SESSION_ASN1, tlsext_tick_lifetime_hint, ZLONG, 9),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, tlsext_tick, ASN1_OCTET_STRING, 10),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, comp_id, ASN1_OCTET_STRING, 11),
#ifndef OPENSSL_NO_SRP
    ASN1_EXP_OPT(SSL_SESSION_ASN1, srp_username, ASN1_OCTET_STRING, 12),
#endif
    ASN1_EXP_OPT(SSL_SESSION_ASN1, flags, ZLONG, 13),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, tlsext_tick_age_add, ZLONG, 14),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, max_early_data, ZLONG, 15),
    ASN1_EXP_OPT(SSL_SESSION_ASN1, alpn_selected, ASN1_OCTET_STRING, 16)
} static_ASN1_SEQUENCE_END(SSL_SESSION_ASN1)

IMPLEMENT_STATIC_ASN1_ENCODE_FUNCTIONS(SSL_SESSION_ASN1)

/* Utility functions for i2d_SSL_SESSION */

/* Initialise OCTET STRING from buffer and length */

static void ssl_session_oinit(ASN1_OCTET_STRING **dest, ASN1_OCTET_STRING *os,
                              unsigned char *data, size_t len)
{
    os->data = data;
    os->length = (int)len;
    os->flags = 0;
    *dest = os;
}

/* Initialise OCTET STRING from string */
static void ssl_session_sinit(ASN1_OCTET_STRING **dest, ASN1_OCTET_STRING *os,
                              char *data)
{
    if (data != NULL)
        ssl_session_oinit(dest, os, (unsigned char *)data, strlen(data));
    else
        *dest = NULL;
}

int i2d_SSL_SESSION(SSL_SESSION *in, unsigned char **pp)
{

    SSL_SESSION_ASN1 as;

    ASN1_OCTET_STRING cipher;
    unsigned char cipher_data[2];
    ASN1_OCTET_STRING master_key, session_id, sid_ctx;

#ifndef OPENSSL_NO_COMP
    ASN1_OCTET_STRING comp_id;
    unsigned char comp_id_data;
#endif
    ASN1_OCTET_STRING tlsext_hostname, tlsext_tick;
#ifndef OPENSSL_NO_SRP
    ASN1_OCTET_STRING srp_username;
#endif
#ifndef OPENSSL_NO_PSK
    ASN1_OCTET_STRING psk_identity, psk_identity_hint;
#endif
    ASN1_OCTET_STRING alpn_selected;

    long l;

    if ((in == NULL) || ((in->cipher == NULL) && (in->cipher_id == 0)))
        return 0;

    memset(&as, 0, sizeof(as));

    as.version = SSL_SESSION_ASN1_VERSION;
    as.ssl_version = in->ssl_version;

    if (in->cipher == NULL)
        l = in->cipher_id;
    else
        l = in->cipher->id;
    cipher_data[0] = ((unsigned char)(l >> 8L)) & 0xff;
    cipher_data[1] = ((unsigned char)(l)) & 0xff;

    ssl_session_oinit(&as.cipher, &cipher, cipher_data, 2);

#ifndef OPENSSL_NO_COMP
    if (in->compress_meth) {
        comp_id_data = (unsigned char)in->compress_meth;
        ssl_session_oinit(&as.comp_id, &comp_id, &comp_id_data, 1);
    }
#endif

    ssl_session_oinit(&as.master_key, &master_key,
                      in->master_key, in->master_key_length);

    ssl_session_oinit(&as.session_id, &session_id,
                      in->session_id, in->session_id_length);

    ssl_session_oinit(&as.session_id_context, &sid_ctx,
                      in->sid_ctx, in->sid_ctx_length);

    as.time = in->time;
    as.timeout = in->timeout;
    as.verify_result = in->verify_result;

    as.peer = in->peer;

    ssl_session_sinit(&as.tlsext_hostname, &tlsext_hostname,
                      in->ext.hostname);
    if (in->ext.tick) {
        ssl_session_oinit(&as.tlsext_tick, &tlsext_tick,
                          in->ext.tick, in->ext.ticklen);
    }
    if (in->ext.tick_lifetime_hint > 0)
        as.tlsext_tick_lifetime_hint = in->ext.tick_lifetime_hint;
    as.tlsext_tick_age_add = in->ext.tick_age_add;
#ifndef OPENSSL_NO_PSK
    ssl_session_sinit(&as.psk_identity_hint, &psk_identity_hint,
                      in->psk_identity_hint);
    ssl_session_sinit(&as.psk_identity, &psk_identity, in->psk_identity);
#endif                          /* OPENSSL_NO_PSK */
#ifndef OPENSSL_NO_SRP
    ssl_session_sinit(&as.srp_username, &srp_username, in->srp_username);
#endif                          /* OPENSSL_NO_SRP */

    as.flags = in->flags;
    as.max_early_data = in->ext.max_early_data;

    if (in->ext.alpn_selected == NULL)
        as.alpn_selected = NULL;
    else
        ssl_session_oinit(&as.alpn_selected, &alpn_selected,
                          in->ext.alpn_selected, in->ext.alpn_selected_len);

    return i2d_SSL_SESSION_ASN1(&as, pp);

}

/* Utility functions for d2i_SSL_SESSION */

/* OPENSSL_strndup an OCTET STRING */

static int ssl_session_strndup(char **pdst, ASN1_OCTET_STRING *src)
{
    OPENSSL_free(*pdst);
    *pdst = NULL;
    if (src == NULL)
        return 1;
    *pdst = OPENSSL_strndup((char *)src->data, src->length);
    if (*pdst == NULL)
        return 0;
    return 1;
}

/* Copy an OCTET STRING, return error if it exceeds maximum length */

static int ssl_session_memcpy(unsigned char *dst, size_t *pdstlen,
                              ASN1_OCTET_STRING *src, size_t maxlen)
{
    if (src == NULL) {
        *pdstlen = 0;
        return 1;
    }
    if (src->length < 0 || src->length > (int)maxlen)
        return 0;
    memcpy(dst, src->data, src->length);
    *pdstlen = src->length;
    return 1;
}

SSL_SESSION *d2i_SSL_SESSION(SSL_SESSION **a, const unsigned char **pp,
                             long length)
{
    long id;
    size_t tmpl;
    const unsigned char *p = *pp;
    SSL_SESSION_ASN1 *as = NULL;
    SSL_SESSION *ret = NULL;

    as = d2i_SSL_SESSION_ASN1(NULL, &p, length);
    /* ASN.1 code returns suitable error */
    if (as == NULL)
        goto err;

    if (!a || !*a) {
        ret = SSL_SESSION_new();
        if (ret == NULL)
            goto err;
    } else {
        ret = *a;
    }

    if (as->version != SSL_SESSION_ASN1_VERSION) {
        SSLerr(SSL_F_D2I_SSL_SESSION, SSL_R_UNKNOWN_SSL_VERSION);
        goto err;
    }

    if ((as->ssl_version >> 8) != SSL3_VERSION_MAJOR
        && (as->ssl_version >> 8) != DTLS1_VERSION_MAJOR
        && as->ssl_version != DTLS1_BAD_VER) {
        SSLerr(SSL_F_D2I_SSL_SESSION, SSL_R_UNSUPPORTED_SSL_VERSION);
        goto err;
    }

    ret->ssl_version = (int)as->ssl_version;

    if (as->cipher->length != 2) {
        SSLerr(SSL_F_D2I_SSL_SESSION, SSL_R_CIPHER_CODE_WRONG_LENGTH);
        goto err;
    }

    id = 0x03000000L | ((unsigned long)as->cipher->data[0] << 8L)
                     | (unsigned long)as->cipher->data[1];

    ret->cipher_id = id;
    ret->cipher = ssl3_get_cipher_by_id(id);
    if (ret->cipher == NULL)
        goto err;

    if (!ssl_session_memcpy(ret->session_id, &ret->session_id_length,
                            as->session_id, SSL3_MAX_SSL_SESSION_ID_LENGTH))
        goto err;

    if (!ssl_session_memcpy(ret->master_key, &tmpl,
                            as->master_key, TLS13_MAX_RESUMPTION_MASTER_LENGTH))
        goto err;

    ret->master_key_length = tmpl;

    if (as->time != 0)
        ret->time = as->time;
    else
        ret->time = (unsigned long)time(NULL);

    if (as->timeout != 0)
        ret->timeout = as->timeout;
    else
        ret->timeout = 3;

    X509_free(ret->peer);
    ret->peer = as->peer;
    as->peer = NULL;

    if (!ssl_session_memcpy(ret->sid_ctx, &ret->sid_ctx_length,
                            as->session_id_context, SSL_MAX_SID_CTX_LENGTH))
        goto err;

    /* NB: this defaults to zero which is X509_V_OK */
    ret->verify_result = as->verify_result;

    if (!ssl_session_strndup(&ret->ext.hostname, as->tlsext_hostname))
        goto err;

#ifndef OPENSSL_NO_PSK
    if (!ssl_session_strndup(&ret->psk_identity_hint, as->psk_identity_hint))
        goto err;
    if (!ssl_session_strndup(&ret->psk_identity, as->psk_identity))
        goto err;
#endif

    ret->ext.tick_lifetime_hint = as->tlsext_tick_lifetime_hint;
    ret->ext.tick_age_add = as->tlsext_tick_age_add;
    if (as->tlsext_tick) {
        ret->ext.tick = as->tlsext_tick->data;
        ret->ext.ticklen = as->tlsext_tick->length;
        as->tlsext_tick->data = NULL;
    } else {
        ret->ext.tick = NULL;
    }
#ifndef OPENSSL_NO_COMP
    if (as->comp_id) {
        if (as->comp_id->length != 1) {
            SSLerr(SSL_F_D2I_SSL_SESSION, SSL_R_BAD_LENGTH);
            goto err;
        }
        ret->compress_meth = as->comp_id->data[0];
    } else {
        ret->compress_meth = 0;
    }
#endif

#ifndef OPENSSL_NO_SRP
    if (!ssl_session_strndup(&ret->srp_username, as->srp_username))
        goto err;
#endif                          /* OPENSSL_NO_SRP */
    /* Flags defaults to zero which is fine */
    ret->flags = as->flags;
    ret->ext.max_early_data = as->max_early_data;

    if (as->alpn_selected != NULL) {
        if (!ssl_session_strndup((char **)&ret->ext.alpn_selected,
                                 as->alpn_selected))
            goto err;
        ret->ext.alpn_selected_len = as->alpn_selected->length;
    } else {
        ret->ext.alpn_selected = NULL;
        ret->ext.alpn_selected_len = 0;
    }

    M_ASN1_free_of(as, SSL_SESSION_ASN1);

    if ((a != NULL) && (*a == NULL))
        *a = ret;
    *pp = p;
    return ret;

 err:
    M_ASN1_free_of(as, SSL_SESSION_ASN1);
    if ((a == NULL) || (*a != ret))
        SSL_SESSION_free(ret);
    return NULL;
}
