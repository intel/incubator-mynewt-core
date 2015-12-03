/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "os/os_mempool.h"
#include "nimble/ble.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "ble_hs_uuid.h"
#include "ble_hs_conn.h"
#include "ble_att_cmd.h"
#include "ble_att.h"

#define BLE_ATT_CLT_NUM_ENTRIES  128
static void *ble_att_clt_entry_mem;
static struct os_mempool ble_att_clt_entry_pool;

static struct ble_att_clt_entry *
ble_att_clt_entry_alloc(void)
{
    struct ble_att_clt_entry *entry;

    entry = os_memblock_get(&ble_att_clt_entry_pool);
    if (entry != NULL) {
        memset(entry, 0, sizeof *entry);
    }

    return entry;
}

static void
ble_att_clt_entry_free(struct ble_att_clt_entry *entry)
{
    int rc;

    rc = os_memblock_put(&ble_att_clt_entry_pool, entry);
    assert(rc == 0);
}

void
ble_att_clt_entry_list_free(struct ble_att_clt_entry_list *list)
{
    struct ble_att_clt_entry *entry;

    while ((entry = SLIST_FIRST(list)) != NULL) {
        SLIST_REMOVE_HEAD(list, bhac_next);
        ble_att_clt_entry_free(entry);
    }
}

int
ble_att_clt_entry_insert(struct ble_hs_conn *conn, uint16_t handle_id,
                         uint8_t *uuid)
{
    struct ble_att_clt_entry *entry;
    struct ble_att_clt_entry *prev;
    struct ble_att_clt_entry *cur;

    /* XXX: Probably need to lock a semaphore here. */

    entry = ble_att_clt_entry_alloc();
    if (entry == NULL) {
        return ENOMEM;
    }

    entry->bhac_handle_id = handle_id;
    memcpy(entry->bhac_uuid, uuid, sizeof entry->bhac_uuid);

    prev = NULL;
    SLIST_FOREACH(cur, &conn->bhc_att_clt_list, bhac_next) {
        if (cur->bhac_handle_id == handle_id) {
            return EEXIST;
        }
        if (cur->bhac_handle_id > handle_id) {
            break;
        }

        prev = cur;
    }

    if (prev == NULL) {
        SLIST_INSERT_HEAD(&conn->bhc_att_clt_list, entry, bhac_next);
    } else {
        SLIST_INSERT_AFTER(prev, entry, bhac_next);
    }

    return 0;
}

uint16_t
ble_att_clt_find_entry_uuid128(struct ble_hs_conn *conn, void *uuid128)
{
    struct ble_att_clt_entry *entry;
    int rc;

    SLIST_FOREACH(entry, &conn->bhc_att_clt_list, bhac_next) {
        rc = memcmp(entry->bhac_uuid, uuid128, 16);
        if (rc == 0) {
            return entry->bhac_handle_id;
        }
    }

    return 0;
}

static int
ble_att_clt_prep_req(struct ble_hs_conn *conn, struct ble_l2cap_chan **chan,
                     struct os_mbuf **txom, uint16_t initial_sz)
{
    void *buf;
    int rc;

    *chan = ble_hs_conn_chan_find(conn, BLE_L2CAP_CID_ATT);
    assert(*chan != NULL);

    *txom = os_mbuf_get_pkthdr(&ble_hs_mbuf_pool, 0);
    if (*txom == NULL) {
        rc = ENOMEM;
        goto err;
    }

    buf = os_mbuf_extend(*txom, initial_sz);
    if (buf == NULL) {
        rc = ENOMEM;
        goto err;
    }

    /* The caller expects the initial buffer to be at the start of the mbuf. */
    assert(buf == (*txom)->om_data);

    return 0;

err:
    os_mbuf_free_chain(*txom);
    *txom = NULL;
    return rc;
}

uint16_t
ble_att_clt_find_entry_uuid16(struct ble_hs_conn *conn, uint16_t uuid16)
{
    uint8_t uuid128[16];
    uint16_t handle_id;
    int rc;

    rc = ble_hs_uuid_from_16bit(uuid16, uuid128);
    if (rc != 0) {
        return 0;
    }

    handle_id = ble_att_clt_find_entry_uuid128(conn, uuid128);
    return handle_id;
}

int
ble_att_clt_tx_mtu(struct ble_hs_conn *conn, struct ble_att_mtu_cmd *req)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bhamc_mtu < BLE_ATT_MTU_DFLT) {
        rc = EINVAL;
        goto err;
    }

    rc = ble_att_clt_prep_req(conn, &chan, &txom, BLE_ATT_MTU_CMD_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_mtu_req_write(txom->om_data, txom->om_len, req);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_clt_rx_mtu(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                   struct os_mbuf *om)
{
    struct ble_att_mtu_cmd rsp;
    int rc;

    /* XXX: Pull up om */

    rc = ble_att_mtu_cmd_parse(om->om_data, om->om_len, &rsp);
    if (rc != 0) {
        return rc;
    }

    ble_att_set_peer_mtu(chan, rsp.bhamc_mtu);

    ble_gatt_rx_mtu(conn, ble_l2cap_chan_mtu(chan));

    return 0;
}

int
ble_att_clt_tx_find_info(struct ble_hs_conn *conn,
                         struct ble_att_find_info_req *req)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bhafq_start_handle == 0 ||
        req->bhafq_start_handle > req->bhafq_end_handle) {

        rc = EINVAL;
        goto err;
    }

    rc = ble_att_clt_prep_req(conn, &chan, &txom,
                                 BLE_ATT_FIND_INFO_REQ_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_find_info_req_write(txom->om_data, txom->om_len, req);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_clt_rx_find_info(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                         struct os_mbuf *om)
{
    struct ble_att_find_info_rsp rsp;
    uint16_t handle_id;
    uint16_t uuid16;
    uint8_t uuid128[16];
    int off;
    int rc;

    /* XXX: Pull up om */

    rc = ble_att_find_info_rsp_parse(om->om_data, om->om_len, &rsp);
    if (rc != 0) {
        return rc;
    }

    handle_id = 0;
    off = BLE_ATT_FIND_INFO_RSP_MIN_SZ;
    while (off < OS_MBUF_PKTHDR(om)->omp_len) {
        rc = os_mbuf_copydata(om, off, 2, &handle_id);
        if (rc != 0) {
            rc = EINVAL;
            goto done;
        }
        off += 2;
        handle_id = le16toh(&handle_id);

        switch (rsp.bhafp_format) {
        case BLE_ATT_FIND_INFO_RSP_FORMAT_16BIT:
            rc = os_mbuf_copydata(om, off, 2, &uuid16);
            if (rc != 0) {
                rc = EINVAL;
                goto done;
            }
            off += 2;
            uuid16 = le16toh(&uuid16);

            rc = ble_hs_uuid_from_16bit(uuid16, uuid128);
            if (rc != 0) {
                rc = EINVAL;
                goto done;
            }
            break;

        case BLE_ATT_FIND_INFO_RSP_FORMAT_128BIT:
            rc = os_mbuf_copydata(om, off, 16, &uuid128);
            if (rc != 0) {
                rc = EINVAL;
                goto done;
            }
            off += 16;
            break;

        default:
            rc = EINVAL;
            goto done;
        }

        rc = ble_att_clt_entry_insert(conn, handle_id, uuid128);
        if (rc != 0) {
            return rc;
        }
    }

    rc = 0;

done:
    ble_gatt_rx_find_info(conn, -rc, handle_id);
    return rc;
}

int
ble_att_clt_tx_read(struct ble_hs_conn *conn, struct ble_att_read_req *req)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bharq_handle == 0) {
        rc = EINVAL;
        goto err;
    }

    rc = ble_att_clt_prep_req(conn, &chan, &txom, BLE_ATT_READ_REQ_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_read_req_write(txom->om_data, txom->om_len, req);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

int
ble_att_clt_tx_read_group_type(struct ble_hs_conn *conn,
                               struct ble_att_read_group_type_req *req,
                               void *uuid128)
{
    struct ble_l2cap_chan *chan;
    struct os_mbuf *txom;
    int rc;

    txom = NULL;

    if (req->bhagq_start_handle == 0 ||
        req->bhagq_start_handle > req->bhagq_end_handle) {

        rc = EINVAL;
        goto err;
    }

    rc = ble_att_clt_prep_req(conn, &chan, &txom,
                                 BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ);
    if (rc != 0) {
        goto err;
    }

    rc = ble_att_read_group_type_req_write(txom->om_data, txom->om_len,
                                              req);
    if (rc != 0) {
        goto err;
    }

    rc = ble_hs_uuid_append(txom, uuid128);
    if (rc != 0) {
        goto err;
    }

    rc = ble_l2cap_tx(chan, txom);
    txom = NULL;
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    os_mbuf_free_chain(txom);
    return rc;
}

struct ble_att_clt_adata {
    uint16_t att_handle;
    uint16_t end_group_handle;
    void *value;
};

static int
ble_att_clt_parse_attribute_data(struct os_mbuf *om, int data_len,
                                 struct ble_att_clt_adata *adata)
{
    /* XXX: Pull up om */

    adata->att_handle = le16toh(om->om_data + 0);
    adata->end_group_handle = le16toh(om->om_data + 2);
    adata->value = om->om_data + 6;

    return 0;
}

int
ble_att_clt_rx_read_group_type_rsp(struct ble_hs_conn *conn,
                                   struct ble_l2cap_chan *chan,
                                   struct os_mbuf *om)
{
    struct ble_att_read_group_type_rsp rsp;
    struct ble_att_clt_adata adata;
    int rc;

    /* XXX: Pull up om */

    rc = ble_att_read_group_type_rsp_parse(om->om_data, om->om_len, &rsp);
    if (rc != 0) {
        return rc;
    }

    /* XXX: Verify group handle is valid. */

    while (OS_MBUF_PKTHDR(om)->omp_len > 0) {
        rc = ble_att_clt_parse_attribute_data(om, rsp.bhagp_length, &adata);
        if (rc != 0) {
            break;
        }

        /* Save attribute mapping? */

        /* XXX: Pass adata to GATT callback. */

        os_mbuf_adj(om, rsp.bhagp_length);
    }

    return 0;
}

int
ble_att_clt_init(void)
{
    int rc;

    free(ble_att_clt_entry_mem);
    ble_att_clt_entry_mem = malloc(
        OS_MEMPOOL_BYTES(BLE_ATT_CLT_NUM_ENTRIES,
                         sizeof (struct ble_att_clt_entry)));
    if (ble_att_clt_entry_mem == NULL) {
        rc = ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&ble_att_clt_entry_pool,
                         BLE_ATT_CLT_NUM_ENTRIES,
                         sizeof (struct ble_att_clt_entry),
                         ble_att_clt_entry_mem,
                         "ble_att_clt_entry_pool");
    if (rc != 0) {
        goto err;
    }

    return 0;

err:
    free(ble_att_clt_entry_mem);
    ble_att_clt_entry_mem = NULL;

    return rc;
}