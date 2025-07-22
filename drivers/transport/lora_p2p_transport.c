/*
 * Copyright (c) 2025 Cerbercomm LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Maintained by:
 *   2025-07-21 Or Goshen
 */

#include "lora_p2p_transport.h"
#include "lora_p2p_transport_layer.h"
#include "lora_p2p_network_layer.h"

#include "lora_p2p_network_direct.h"

#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(P2PTrans, CONFIG_LBM_P2P_TRANSPORT_LOG_LEVEL);

/* Definitions
*/
#define LBM_BUFFER_SIZE_MAX 255

struct lora_p2p_transport_data_t {
    struct device *network_dev;

    // buffer for IO
    uint8_t buffer[LBM_BUFFER_SIZE_MAX];
};

// an Ack packet
#define LBM_TRANSPORT_HEADER_TYPE_ACK         0

// stand alone packet
#define LBM_TRANSPORT_HEADER_TYPE_STAND_ALONE 1

// a starter or finisher of a multi packet train
#define LBM_TRANSPORT_HEADER_TYPE_BOUND       2

// a continuation of a multi packet train
#define LBM_TRANSPORT_HEADER_TYPE_CONTINUE    3

// flag that we want reliable transport (we want an Ack for each send)
#define LBM_TRANSPORT_HEADER_FLAG_RELIABLE    4

// ** LoRa Network device definition **
#define LORA_NETWORK_DEVICE DEVICE_GET(LORA_P2P_NETWORK_DIRECT_DRIVER_NAME)

/* Driver init
*/
static int lora_p2p_transport_init(const struct device *dev) {
    struct lora_p2p_transport_data_t *data = dev->data;

    // initialize inferior network driver
    data->network_dev = LORA_NETWORK_DEVICE;

    return 0;
}

/* Driver API
*/
static struct device * lora_p2p_transport_get_network_device_impl(const struct device *dev) {
    struct lora_p2p_transport_data_t *data = dev->data;

	return data->network_dev;
}

static int lora_p2p_transport_send_impl(const struct device *dev, uint8_t to, struct ring_buf *rb, bool reliable) {
    struct lora_p2p_transport_data_t *data = dev->data;
    uint8_t packet_size;
    int retcode;
    struct lora_p2p_network_incoming_t meta;

    // sanity check: all required parameters were set beforehand
    if (data->network_dev == NULL) return -EINVAL;

    LOG_DBG("Sending %d bytes to %d", 	ring_buf_size_get(rb), to);

    // stand alone packet ?
    if (lora_p2p_network_get_mtu(data->network_dev) >= (ring_buf_size_get(rb)+1)) {
        // header: type
        data->buffer[0] = LBM_TRANSPORT_HEADER_TYPE_STAND_ALONE;

        // header: reliable transport (with Ack for each send)
        data->buffer[0] |= reliable ? LBM_TRANSPORT_HEADER_FLAG_RELIABLE : 0;

        // content
        packet_size = ring_buf_get(rb, &data->buffer[1], lora_p2p_network_get_mtu(data->network_dev)-1);
    
    // and initializer for a packet train ?
    } else {
        // header: type
        data->buffer[0] = LBM_TRANSPORT_HEADER_TYPE_BOUND;

        // header: reliable transport (with Ack for each send)
        data->buffer[0] |= reliable ? LBM_TRANSPORT_HEADER_FLAG_RELIABLE : 0;

        // header: packet train content length in bytes
        data->buffer[1] = ring_buf_size_get(rb) & 0xFF;
        data->buffer[2] = (ring_buf_size_get(rb) >> 8) & 0xFF;

        // content
        packet_size = ring_buf_get(rb, &data->buffer[3], lora_p2p_network_get_mtu(data->network_dev)-3);
    }

    do {
        // send packet
        retcode = lora_p2p_network_send(data->network_dev, to, data->buffer, packet_size);
        if (retcode < 0) return retcode;

        // reliable ?
        if (reliable) {
            // wait for Ack
            retcode = lora_p2p_network_recv(data->network_dev, &meta, &data->buffer[0], LBM_BUFFER_SIZE_MAX);
            
        }

        // are we done ?
        if (ring_buf_is_empty(rb)) break;

        // give recipient one millisecond to sort things out
        k_sleep(K_MSEC(1));

        // ** prepare header **
        // packet type
        data->buffer[0] = lora_p2p_network_get_mtu(data->network_dev) >= (length - pos) ?
            LBM_TRANSPORT_HEADER_TYPE_BOUND : LBM_TRANSPORT_HEADER_TYPE_CONTINUE;

        // reliable ? 
        data->buffer[0] |= reliable ? LBM_TRANSPORT_HEADER_FLAG_RELIABLE : 0;

        // ** content **
        packet_size = lora_p2p_network_get_mtu(data->network_dev) > (length - pos) ?
            (length - pos) : lora_p2p_network_get_mtu(data->network_dev);
        memcpy(&data->buffer[1], &buffer[pos], packet_size);
        pos += packet_size;

    } while (true);

    // return how many bytes we sent
    return pos;
}

static int lora_p2p_transport_recv_impl(const struct device *dev, struct lora_p2p_transport_incoming_t *meta, uint8_t *buffer, uint16_t length) {
	struct lora_p2p_transport_data_t *data = dev->data;

    // sanity check: all required parameters were set beforehand
    if (data->network_dev == NULL) return -EINVAL;

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
static struct lora_p2p_transport_data_t data;

static DEVICE_API(lora_p2p_transport, lora_p2p_transport_api) = {
    .get_network_device = lora_p2p_transport_get_network_device_impl,
    .send = lora_p2p_transport_send_impl,
    .recv = lora_p2p_transport_recv_impl
};

DEVICE_DEFINE(lora_p2p_transport, LORA_P2P_TRANSPORT_DRIVER_NAME, lora_p2p_transport_init,
    NULL, &data, NULL, POST_KERNEL,
    CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &lora_p2p_transport_api);
