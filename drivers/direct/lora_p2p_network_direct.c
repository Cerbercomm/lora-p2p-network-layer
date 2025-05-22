/*
 * Copyright (c) 2025 Cerbercomm LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Maintained by:
 *   2025-05-20 Or Goshen
 */

#include "lora_p2p_network_direct.h"

#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(P2PDirect, CONFIG_LBM_P2P_NETWORK_LOG_LEVEL);

/* Definitions
*/
#define LBM_BUFFER_SIZE 255

struct lora_p2p_network_direct_data_t {
    // this node id
    uint8_t my_id;

    // callbacks
    lora_p2p_network_api_int_send_cb send;
    lora_p2p_network_api_int_recv_cb recv;

    // userdata for callbacks
    void *user_data;

    // IO buffer
    uint8_t buffer[LBM_BUFFER_SIZE];
};

/* Driver init
*/

/* Driver API
*/
static int lora_p2p_network_set_callbacks_direct(const struct device *dev, lora_p2p_network_api_int_send_cb send, lora_p2p_network_api_int_recv_cb recv, void *user_data) {
	struct lora_p2p_network_direct_data_t *data = dev->data;
    
    LOG_DBG("Callbacks are set");

    data->recv = recv;
    data->send = send;

    data->user_data = user_data;

    return 0;
}

static int lora_p2p_network_set_node_id_direct(const struct device *dev, uint8_t node_id) {
    struct lora_p2p_network_direct_data_t *data = dev->data;

    LOG_DBG("My node id is set to %d", node_id);

    data->my_id = node_id;

	return 0;
}

static int lora_p2p_network_send_direct(const struct device *dev, uint8_t to, const uint8_t *buffer, uint8_t length) {
    struct lora_p2p_network_direct_data_t *data = dev->data;

    // sanity check: all required parameters were set beforehand
    if (data->recv == NULL || data->send == NULL) return -EINVAL;

    // sanity check: we need enough space in the buffer to prefix our header
    if (length >= (LBM_BUFFER_SIZE - LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH)) return -EINVAL;

    LOG_DBG("Sending %d bytes to %d", length, to);

    // set header (from + to)
    data->buffer[0] = data->my_id;
    data->buffer[1] = to;

    // copy content to be transferred
    memcpy(&data->buffer[2], buffer, length);

    // do the sending
    return data->send(to, data->buffer, length + LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH, data->user_data);
}

static int lora_p2p_network_recv_direct(const struct device *dev, struct lora_p2p_network_incoming_t *meta, uint8_t *buffer, uint8_t length) {
	struct lora_p2p_network_direct_data_t *data = dev->data;

    // sanity check: all required parameters were set beforehand
    if (data->recv == NULL || data->send == NULL) return -EINVAL;

    // sanity check: supplied buffer length MUST be at least big enough to hold the header
    if (length < LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH) return -EINVAL;

    LOG_DBG("Ready to receive %d bytes at most", length);

    // keep trying to recv until we get something for us
    while (true) {
        // do the receiving
        int recv_len = data->recv(data->buffer, LBM_BUFFER_SIZE, &meta->rssi, &meta->snr, data->user_data);

        // error ? return it here
        if (recv_len < 0) return recv_len;

        // is it for us ?
        if (data->my_id != data->buffer[1] && data->buffer[1] != LORA_P2P_BROADCAST_ID) continue;

        // sanity check that we have enough space in supplied buffer
        if (length < (recv_len - LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH)) return -EINVAL;

        // update meta data
        meta->from = data->buffer[0];
        meta->to = data->buffer[1];

        // copy payload
        memcpy(buffer, &data->buffer[2], recv_len - LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH);

        LOG_DBG("Received %d bytes from %d", recv_len - LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH, meta->from);

        // return how many bytes we put in the buffer
        return recv_len - LORA_P2P_NETWORK_DIRECT_HEADER_LENGTH;
    }
}

/* Driver & Device definition
*/
static struct lora_p2p_network_direct_data_t data;

static DEVICE_API(lora_p2p_network, lora_p2p_network_api) = {
    .set_node_id = lora_p2p_network_set_node_id_direct,
    .set_callbacks = lora_p2p_network_set_callbacks_direct,
    .send = lora_p2p_network_send_direct,
    .recv = lora_p2p_network_recv_direct
};

DEVICE_DEFINE(lora_p2p_network_direct, LORA_P2P_NETWORK_DIRECT_DRIVER_NAME, NULL,
    NULL, &data, NULL, POST_KERNEL,
    CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &lora_p2p_network_api);
