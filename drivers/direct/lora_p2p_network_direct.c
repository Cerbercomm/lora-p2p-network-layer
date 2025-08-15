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
#include "zephyr/sys/ring_buffer.h"

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
    // this node id
    uint8_t my_id;
};

struct lora_p2p_network_direct_config_t {
    // link layer lora device
    const struct device *lora_dev;
};

/* Driver init
*/
static int lora_p2p_network_direct_init(const struct device *dev) {
    const struct lora_p2p_network_direct_config_t *config = dev->config;

    // make sure lora device is ready
    if (!device_is_ready(config->lora_dev)) {
        LOG_ERR("%s Device not ready", config->lora_dev->name);
        return -EINVAL;
    }

    LOG_INF("LoRa network layer ready");

    return 0;
}

/* Driver API
*/
static const struct device * lora_p2p_network_get_link_device_direct(const struct device *dev) {
    const struct lora_p2p_network_direct_config_t *config = dev->config;
    return config->lora_dev;
}

static uint32_t lora_p2p_network_get_mtu_direct(const struct device *dev) {
    const struct lora_p2p_network_direct_config_t *config = dev->config;
	return lbm_get_mtu(config->lora_dev)-LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH;
}

static int lora_p2p_network_set_node_id_direct(const struct device *dev, uint8_t node_id) {
    struct lora_p2p_network_direct_data_t *data = dev->data;

    LOG_DBG("My node id is set to %d", node_id);

    data->my_id = node_id;

	return 0;
}

static int lora_p2p_network_send_direct(const struct device *dev, uint8_t to, struct ring_buf *rb) {
    const struct lora_p2p_network_direct_config_t *config = dev->config;
    struct lora_p2p_network_direct_data_t *data = dev->data;

    // sanity check: we need enough space in the buffer to add our header
    if (ring_buf_space_get(rb) < LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH) {
        LOG_ERR("lora_p2p_network_send_direct(): Buffer size too small");
        return -ENOMEM;
    }

    // sanity check: size is not bigger than the hardware MTU
    if ((ring_buf_size_get(rb) + LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH) > lbm_get_mtu(config->lora_dev)) {
        LOG_ERR("lora_p2p_network_send_direct(): Capacity bigger than hardware MTU");
        return -ENOMEM;
    }

    LOG_DBG("Sending %d bytes to %d", ring_buf_size_get(rb), to);

    // set header (from + to)
    uint8_t ch;
    ch = data->my_id; ring_buf_put(rb, &ch, 1);
    ch = to;          ring_buf_put(rb, &ch, 1);

    // claim all ring buffer contents
    uint8_t *packet;
    uint32_t packet_size = ring_buf_get_claim(rb, &packet, ring_buf_size_get(rb));

    // do the sending
    int retcode = lbm_send(config->lora_dev, packet, packet_size);

    // finish the claim
    ring_buf_get_finish(rb, packet_size);

    // return the return code from the send() operation
    return retcode;
}

static int lora_p2p_network_recv_direct(const struct device *dev, struct lora_p2p_network_incoming_t *meta, struct ring_buf *rb, k_timeout_t timeout) {
	const struct lora_p2p_network_direct_config_t *config = dev->config;
    struct lora_p2p_network_direct_data_t *data = dev->data;

    // sanity check: free space must be at least as big as a header
    if (ring_buf_space_get(rb) < LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH) {
        LOG_ERR("lora_p2p_network_recv_direct(): Buffer size too small");
        return -ENOMEM;
    }

    LOG_DBG("Ready to receive up to %d bytes", ring_buf_space_get(rb));

    // claim at most MTU amount of bytes
    uint8_t *packet, from, to;
    uint32_t available_size = ring_buf_put_claim(rb, &packet, lbm_get_mtu(config->lora_dev));

    // keep trying to recv until we get something for us
    while (true) {
        // do the receiving
        int recv_len = lbm_recv(config->lora_dev, packet, available_size, timeout, &meta->rssi, &meta->snr);

        // error ? return it here
        if (recv_len < 0) return recv_len;

        from = packet[recv_len-2];
        to = packet[recv_len-1];

        LOG_DBG("Got packet (size = %d, from = %d, to = %d)", recv_len, from, to);

        // is it for us ?
        if (data->my_id != to && to != LORA_P2P_BROADCAST_ID) continue;

        // update meta data
        meta->from = from;
        meta->to = to;

        // finish the claim (get rid of the header while at it)
        if (ring_buf_put_finish(rb, recv_len-LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH) < 0) {
            LOG_ERR("lora_p2p_network_recv_direct(): Recv too big");
            return -ENOMEM;
        }
        
        LOG_DBG("Received %d bytes from %d", ring_buf_size_get(rb), meta->from);

        // return how many bytes we have available
        return ring_buf_size_get(rb);
    }
}

/* Driver & Device definition
*/
static struct lora_p2p_network_direct_data_t data;

static const struct lora_p2p_network_direct_config_t config = {
    .lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0))
};

static DEVICE_API(lora_p2p_network, lora_p2p_network_api) = {
    .get_link_device = lora_p2p_network_get_link_device_direct,
    .get_mtu =         lora_p2p_network_get_mtu_direct,
    .set_node_id =     lora_p2p_network_set_node_id_direct,
    .send =            lora_p2p_network_send_direct,
    .recv =            lora_p2p_network_recv_direct
};

DEVICE_DEFINE(lora_p2p_network_direct, LORA_P2P_NETWORK_DRIVER_NAME, lora_p2p_network_direct_init,
    NULL, &data, &config, POST_KERNEL,
    LBM_P2P_NETWORK_INIT_PRIORITY, &lora_p2p_network_api);
