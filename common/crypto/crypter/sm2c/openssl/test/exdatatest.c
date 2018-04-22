/*
 * Copyright 2015-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/crypto.h>

#include "testutil.h"

static long saved_argl;
static void *saved_argp;
static int saved_idx;
static int saved_idx2;
static int gbl_result;

/*
 * SIMPLE EX_DATA IMPLEMENTATION
 * Apps explicitly set/get ex_data as needed
 */

static void exnew(void *parent, void *ptr, CRYPTO_EX_DATA *ad,
          int idx, long argl, void *argp)
{
    if (!TEST_int_eq(idx, saved_idx)
        || !TEST_long_eq(argl, saved_argl)
        || !TEST_ptr_eq(argp, saved_argp)
        || !TEST_ptr_null(ptr))
        gbl_result = 0;
}

static int exdup(CRYPTO_EX_DATA *to, const CRYPTO_EX_DATA *from,
          void *from_d, int idx, long argl, void *argp)
{
    if (!TEST_int_eq(idx, saved_idx)
        || !TEST_long_eq(argl, saved_argl)
        || !TEST_ptr_eq(argp, saved_argp)
        || !TEST_ptr(from_d))
        gbl_result = 0;
    return 1;
}

static void exfree(void *parent, void *ptr, CRYPTO_EX_DATA *ad,
            int idx, long argl, void *argp)
{
    if (!TEST_int_eq(idx, saved_idx)
        || !TEST_long_eq(argl, saved_argl)
        || !TEST_ptr_eq(argp, saved_argp))
        gbl_result = 0;
}

/*
 * PRE-ALLOCATED EX_DATA IMPLEMENTATION
 * Extended data structure is allocated in exnew2/freed in exfree2
 * Data is stored inside extended data structure
 */

typedef struct myobj_ex_data_st {
    char *hello;
    int new;
    int dup;
} MYOBJ_EX_DATA;

static void exnew2(void *parent, void *ptr, CRYPTO_EX_DATA *ad,
          int idx, long argl, void *argp)
{
    MYOBJ_EX_DATA *ex_data = OPENSSL_zalloc(sizeof(*ex_data));
    if (!TEST_int_eq(idx, saved_idx2)
        || !TEST_long_eq(argl, saved_argl)
        || !TEST_ptr_eq(argp, saved_argp)
        || !TEST_ptr_null(ptr)
        || !TEST_ptr(ex_data)
        || !TEST_true(CRYPTO_set_ex_data(ad, saved_idx2, ex_data))) {
        gbl_result = 0;
        OPENSSL_free(ex_data);
    } else {
        ex_data->new = 1;
    }
}

static int exdup2(CRYPTO_EX_DATA *to, const CRYPTO_EX_DATA *from,
          void *from_d, int idx, long argl, void *argp)
{
    MYOBJ_EX_DATA **update_ex_data = (MYOBJ_EX_DATA**)from_d;
    MYOBJ_EX_DATA *ex_data = CRYPTO_get_ex_data(to, saved_idx2);
    if (!TEST_int_eq(idx, saved_idx2)
        || !TEST_long_eq(argl, saved_argl)
        || !TEST_ptr_eq(argp, saved_argp)
        || !TEST_ptr(from_d)
        || !TEST_ptr(*update_ex_data)
        || !TEST_ptr(ex_data)
        || !TEST_true(ex_data->new)) {
        gbl_result = 0;
    } else {
        /* Copy hello over */
        ex_data->hello = (*update_ex_data)->hello;
        /* indicate this is a dup */
        ex_data->dup = 1;
        /* Keep my original ex_data */
        *update_ex_data = ex_data;
    }
    return 1;
}

static void exfree2(void *parent, void *ptr, CRYPTO_EX_DATA *ad,
            int idx, long argl, void *argp)
{
    MYOBJ_EX_DATA *ex_data = CRYPTO_get_ex_data(ad, saved_idx2);
    OPENSSL_free(ex_data);
    if (!TEST_int_eq(idx, saved_idx2)
        || !TEST_long_eq(argl, saved_argl)
        || !TEST_ptr_eq(argp, saved_argp)
        || !TEST_ptr(ex_data)
        || !TEST_true(CRYPTO_set_ex_data(ad, saved_idx2, NULL)))
        gbl_result = 0;
}

typedef struct myobj_st {
    CRYPTO_EX_DATA ex_data;
    int id;
    int st;
} MYOBJ;

static MYOBJ *MYOBJ_new()
{
    static int count = 0;
    MYOBJ *obj = OPENSSL_malloc(sizeof(*obj));

    obj->id = ++count;
    obj->st = CRYPTO_new_ex_data(CRYPTO_EX_INDEX_APP, obj, &obj->ex_data);
    return obj;
}

static void MYOBJ_sethello(MYOBJ *obj, char *cp)
{
    obj->st = CRYPTO_set_ex_data(&obj->ex_data, saved_idx, cp);
    if (!TEST_int_eq(obj->st, 1))
        gbl_result = 0;
}

static char *MYOBJ_gethello(MYOBJ *obj)
{
    return CRYPTO_get_ex_data(&obj->ex_data, saved_idx);
}

static void MYOBJ_sethello2(MYOBJ *obj, char *cp)
{
    MYOBJ_EX_DATA* ex_data = CRYPTO_get_ex_data(&obj->ex_data, saved_idx2);
    if (TEST_ptr(ex_data))
        ex_data->hello = cp;
    else
        obj->st = gbl_result = 0;
}

static char *MYOBJ_gethello2(MYOBJ *obj)
{
    MYOBJ_EX_DATA* ex_data = CRYPTO_get_ex_data(&obj->ex_data, saved_idx2);
    if (TEST_ptr(ex_data))
        return ex_data->hello;

    obj->st = gbl_result = 0;
    return NULL;
}

static void MYOBJ_free(MYOBJ *obj)
{
    CRYPTO_free_ex_data(CRYPTO_EX_INDEX_APP, obj, &obj->ex_data);
    OPENSSL_free(obj);
}

static MYOBJ *MYOBJ_dup(MYOBJ *in)
{
    MYOBJ *obj = MYOBJ_new();

    obj->st |= CRYPTO_dup_ex_data(CRYPTO_EX_INDEX_APP, &obj->ex_data,
                                 &in->ex_data);
    return obj;
}

static int test_exdata(void)
{
    MYOBJ *t1, *t2, *t3;
    MYOBJ_EX_DATA *ex_data;
    const char *cp;
    char *p;

    gbl_result = 1;

    p = OPENSSL_strdup("hello world");
    saved_argl = 21;
    saved_argp = OPENSSL_malloc(1);
    saved_idx = CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_APP,
                                        saved_argl, saved_argp,
                                        exnew, exdup, exfree);
    saved_idx2 = CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_APP,
                                         saved_argl, saved_argp,
                                         exnew2, exdup2, exfree2);
    t1 = MYOBJ_new();
    t2 = MYOBJ_new();
    if (!TEST_int_eq(t1->st, 1) || !TEST_int_eq(t2->st, 1))
        return 0;
    if (!TEST_ptr(CRYPTO_get_ex_data(&t1->ex_data, saved_idx2)))
        return 0;
    if (!TEST_ptr(CRYPTO_get_ex_data(&t2->ex_data, saved_idx2)))
        return 0;

    MYOBJ_sethello(t1, p);
    cp = MYOBJ_gethello(t1);
    if (!TEST_ptr_eq(cp, p))
        return 0;

    MYOBJ_sethello2(t1, p);
    cp = MYOBJ_gethello2(t1);
    if (!TEST_ptr_eq(cp, p))
        return 0;

    cp = MYOBJ_gethello(t2);
    if (!TEST_ptr_null(cp))
        return 0;

    cp = MYOBJ_gethello2(t2);
    if (!TEST_ptr_null(cp))
        return 0;

    t3 = MYOBJ_dup(t1);
    if (!TEST_int_eq(t3->st, 1))
        return 0;

    ex_data = CRYPTO_get_ex_data(&t3->ex_data, saved_idx2);
    if (!TEST_ptr(ex_data))
        return 0;
    if (!TEST_int_eq(ex_data->dup, 1))
        return 0;

    cp = MYOBJ_gethello(t3);
    if (!TEST_ptr_eq(cp, p))
        return 0;

    cp = MYOBJ_gethello2(t3);
    if (!TEST_ptr_eq(cp, p))
        return 0;

    MYOBJ_free(t1);
    MYOBJ_free(t2);
    MYOBJ_free(t3);
    OPENSSL_free(saved_argp);
    OPENSSL_free(p);

    if (gbl_result)
      return 1;
    else
      return 0;
}

int setup_tests(void)
{
    ADD_TEST(test_exdata);
    return 1;
}