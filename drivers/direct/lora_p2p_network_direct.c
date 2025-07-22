/*
 * Copyright (c) 2025 Cerbercomm LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Maintained by:
 *   2025-05-20 Or Goshen
 */

#include "lora_p2p_network_direct.h"
#include "lora_p2p_network_layer.h"

#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include <lbm_p2p.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(P2PDirect, CONFIG_LBM_P2P_NETWORK_LOG_LEVEL);

/* Definitions
*/
#define LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH 2

struct lora_p2p_network_direct_data_t {
    // link layer lora device
    const struct device *lora_dev;

    // this node id
    uint8_t my_id;
};

/* Driver init
*/
static int lora_p2p_network_direct_init(const struct device *dev) {
    struct lora_p2p_network_direct_data_t *data = dev->data;

    // we'll need a hardware lora device handle
    data->lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));

    return 0;
}

/* Driver API
*/
static uint32_t lora_p2p_network_get_mtu_direct(const struct device *dev) {
    struct lora_p2p_network_direct_data_t *data = dev->data;
	return lbm_get_mtu(data->lora_dev);
}

static int lora_p2p_network_set_node_id_direct(const struct device *dev, uint8_t node_id) {
    struct lora_p2p_network_direct_data_t *data = dev->data;

    LOG_DBG("My node id is set to %d", node_id);

    data->my_id = node_id;

	return 0;
}

static int lora_p2p_network_send_direct(const struct device *dev, uint8_t to, const uint8_t *buffer, uint16_t length) {
    struct lora_p2p_network_direct_data_t *data = dev->data;

    // sanity check: we need enough space in the buffer to prefix our header
    if (length > lora_p2p_network_get_mtu_direct(dev)) {
        LOG_ERR("lora_p2p_network_send_direct(): Buffer size too small");
        return -ENOMEM;
    }

    LOG_DBG("Sending %d bytes to %d", length, to);

    // set header (from + to)
    uint8_t buf[length+LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH]; // FIXME: allocate enough space on the stack (might need to adjust stack size)
    buf[0] = data->my_id;
    buf[1] = to;

    // copy content to be transferred
    memcpy(&buf[2], buffer, length);

    // do the sending
    return lbm_send(data->lora_dev, buf, length + LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH);
}

static int lora_p2p_network_recv_direct(const struct device *dev, struct lora_p2p_network_incoming_t *meta, uint8_t *buffer, uint16_t length, k_timeout_t timeout) {
	struct lora_p2p_network_direct_data_t *data = dev->data;

    // sanity check: supplied buffer length MUST be at least big enough to hold the header
    if (length < LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH) {
        LOG_ERR("lora_p2p_network_recv_direct(): Buffer size too small");
        return -ENOMEM;
    }

    LOG_DBG("Ready to receive %d bytes at most", length);

    // keep trying to recv until we get something for us
    while (true) {
        // do the receiving
        int recv_len = lbm_recv(data->lora_dev, buffer, length, timeout, &meta->rssi, &meta->snr);

        // error ? return it here
        if (recv_len < 0) return recv_len;

        // is it for us ?
        if (data->my_id != buffer[1] && buffer[1] != LORA_P2P_BROADCAST_ID) continue;

        // sanity check that we have enough space in supplied buffer
        if (length < (recv_len - LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH)) return -EINVAL;

        // update meta data
        meta->from = buffer[0];
        meta->to = buffer[1];

        // get rid of the header
        memmove(&buffer[0], &buffer[LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH], recv_len - LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH);

        LOG_DBG("Received %d bytes from %d", recv_len - LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH, meta->from);

        // return how many bytes we put in the buffer
        return recv_len - LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH;
    }
}

/* Driver & Device definition
*/
static struct lora_p2p_network_direct_data_t data;

static DEVICE_API(lora_p2p_network, lora_p2p_network_api) = {
    .get_mtu =     lora_p2p_network_get_mtu_direct,
    .set_node_id = lora_p2p_network_set_node_id_direct,
    .send =        lora_p2p_network_send_direct,
    .recv =        lora_p2p_network_recv_direct
};

DEVICE_DEFINE(lora_p2p_network_direct, LORA_P2P_NETWORK_DIRECT_DRIVER_NAME, lora_p2p_network_direct_init,
    NULL, &data, NULL, POST_KERNEL,
    CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &lora_p2p_network_api);
