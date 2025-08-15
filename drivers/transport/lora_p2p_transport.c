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

#include "zephyr/sys/ring_buffer.h"

#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(P2PTrans, CONFIG_LBM_P2P_TRANSPORT_LOG_LEVEL);

/* Definitions
*/
// buffer size depends on LoRa hardware
#if defined(CONFIG_LORA_BASICS_MODEM_SX126X) || defined(CONFIG_LORA_BASICS_MODEM_SX127X)
# define LBM_BUFFER_SIZE_MAX 255
#else
# error "LoRa Hardware is not defined"
#endif
static uint8_t packet_buffer[LBM_BUFFER_SIZE_MAX];

struct lora_p2p_transport_data_t {
    // ring buffer for IO
    struct ring_buf rb;

    // network layer lora device
    const struct device *lora_network_dev;
};

// ** Header **
// mask for packet type
#define LBM_TRANSPORT_HEADER_TYPE_MASK        0b111

// an Ack packet
#define LBM_TRANSPORT_HEADER_TYPE_ACK         1

// stand alone packet
#define LBM_TRANSPORT_HEADER_TYPE_STAND_ALONE 2

// a starter of a multi packet train
#define LBM_TRANSPORT_HEADER_TYPE_STARTER     3

// a continuation of a multi packet train
#define LBM_TRANSPORT_HEADER_TYPE_CONTINUE    4

// a finisher of a multi packet train
#define LBM_TRANSPORT_HEADER_TYPE_FINISHER    5

// flag that we want reliable transport (we want an Ack for each send)
#define LBM_TRANSPORT_HEADER_FLAG_RELIABLE    0b1000

// ** LoRa Network device definition **
#define LORA_NETWORK_DEVICE DEVICE_GET(LORA_P2P_NETWORK_DRIVER_NAME)

/* Internal
*/
static bool is_ack_packet(struct ring_buf *rb) {
    uint8_t header;

    // must be just a header
    if (ring_buf_size_get(rb) != 1) return false;

    // get header
    ring_buf_get(rb, &header, 1);

    // check type
    return (header & LBM_TRANSPORT_HEADER_TYPE_MASK) == LBM_TRANSPORT_HEADER_TYPE_ACK;
}

static void prepare_ack(struct ring_buf *rb) {
    uint8_t header = LBM_TRANSPORT_HEADER_TYPE_ACK;

    ring_buf_reset(rb);
    ring_buf_put(rb, &header, 1);
}

/* Driver init
*/
static int lora_p2p_transport_init(const struct device *dev) {
    struct lora_p2p_transport_data_t *data = dev->data;

    // assign inferiour network device
    data->lora_network_dev = device_get_binding(LORA_P2P_NETWORK_DRIVER_NAME);

    // make sure lora device is ready
    if (!device_is_ready(data->lora_network_dev)) {
        LOG_ERR("%s Device not ready", data->lora_network_dev->name);
        return -EINVAL;
    }

    // initialize ring buffer
    ring_buf_init(&data->rb, LBM_BUFFER_SIZE_MAX, packet_buffer);

    // ready !
    LOG_INF("LoRa transport layer ready");

    return 0;
}

/* Driver API
*/
static const struct device * lora_p2p_transport_get_network_device_impl(const struct device *dev) {
    struct lora_p2p_transport_data_t *data = dev->data;

	return data->lora_network_dev;
}

static int lora_p2p_transport_send_impl(const struct device *dev, uint8_t to, struct ring_buf *input, bool reliable) {
    struct lora_p2p_transport_data_t *data = dev->data;

    uint8_t *packet;
    uint32_t packet_size, available_size;
    int retcode;
    struct lora_p2p_network_incoming_t meta;
    bool first_packet = true;

    LOG_DBG("Sending %d bytes packet to %d", ring_buf_size_get(input), to);

    do {
        /* Prepare packet
        */
        // reset our buffer so we're at the begining of the memory block
        ring_buf_reset(&data->rb);

        // allocate space for packet
        available_size = ring_buf_put_claim(&data->rb, &packet, lora_p2p_network_get_mtu(data->lora_network_dev));

        // get content to be sent
        packet_size = ring_buf_get(input, packet, available_size-1);

        // first packet we send ?
        if (first_packet) {
            // header: type
            packet[packet_size] = lora_p2p_network_get_mtu(data->lora_network_dev) >= (ring_buf_size_get(input)+1+packet_size) ?
                LBM_TRANSPORT_HEADER_TYPE_STAND_ALONE :
                LBM_TRANSPORT_HEADER_TYPE_STARTER;
            first_packet = false;
        } else {
            // header: type
            packet[packet_size] = lora_p2p_network_get_mtu(data->lora_network_dev) >= (ring_buf_size_get(input)+1+packet_size) ?
                LBM_TRANSPORT_HEADER_TYPE_FINISHER :
                LBM_TRANSPORT_HEADER_TYPE_CONTINUE;
        }

        // header: reliable transport (with Ack for each send)
        packet[packet_size] |= reliable ? LBM_TRANSPORT_HEADER_FLAG_RELIABLE : 0;

        // finish claim
        retcode = ring_buf_put_finish(&data->rb, packet_size+1);

        /* Send packet
        */
        retcode = lora_p2p_network_send(data->lora_network_dev, to, &data->rb);
        if (retcode < 0) return retcode;

        /* Make it reliable if requested
        */
        if (reliable) {
            // reset our buffer just in case
            ring_buf_reset(&data->rb);

            // wait for Ack
            retcode = lora_p2p_network_recv(data->lora_network_dev, &meta, &data->rb, K_SECONDS(1));
            if (retcode < 0) {
                LOG_ERR("Timeout on Ack");
                return retcode;
            }

            // make sure this is an Ack packet
            if (!is_ack_packet(&data->rb)) {
                LOG_ERR("lora_p2p_transport_send_impl(): Expected Ack");
                return -EINVAL;
            }
        }

        /* Aftermath
        */
        // are we done ?
        if (ring_buf_is_empty(input)) break;

        // give recipient grace time of 1 millisecond(s) to sort things out before we work on next part
        k_sleep(K_MSEC(1));

    } while (true);

    // return success
    return 0;
}

static int lora_p2p_transport_recv_impl(const struct device *dev, struct lora_p2p_transport_incoming_t *meta, struct ring_buf *output) {
	struct lora_p2p_transport_data_t *data = dev->data;

    uint8_t header, *packet;
    uint32_t available_size;
    struct lora_p2p_network_incoming_t nmeta;
    int retcode;

    LOG_DBG("Ready to receive %d bytes at most", ring_buf_space_get(output));

    // receive packets while we should
    while (true) {
        /* Receive packet
        */
        // reset ring buffer (we want to point at the start of the memory block)
        ring_buf_reset(&data->rb);

        // receive a packet (wait indefinetly)
        retcode = lora_p2p_network_recv(data->lora_network_dev, &nmeta, &data->rb, K_FOREVER);
        if (retcode < 0) return retcode;

        // update meta data
        meta->from = nmeta.from;
        meta->to = nmeta.to;
        meta->rssi = nmeta.rssi;
        meta->snr = nmeta.snr;

        // claim contents
        available_size = ring_buf_get_claim(&data->rb, &packet, lora_p2p_network_get_mtu(data->lora_network_dev));

        // we MUST have a header
        if (available_size < 1) return -EINVAL;

        // parse header
        header = packet[available_size-1];

        // put contents in caller ring buff
        ring_buf_put(output, packet, available_size-1);

        // finish claim
        ring_buf_get_finish(&data->rb, available_size);

        /* Make it reliable if requested
        */
        if (header & LBM_TRANSPORT_HEADER_FLAG_RELIABLE) {
            // give recipient grace time of one millisecond to sort things out before we send Ack
            k_sleep(K_MSEC(1));

            // prepare Ack packet
            prepare_ack(&data->rb);

            // send Ack
            retcode = lora_p2p_network_send(data->lora_network_dev, nmeta.from, &data->rb);
            if (retcode < 0) return retcode;
        }

        /* Aftermath
        */
        // decide what we do now (we are done if this was a stand alone packet or a finisher)
        if ((header & LBM_TRANSPORT_HEADER_TYPE_MASK) == LBM_TRANSPORT_HEADER_TYPE_STAND_ALONE ||
            (header & LBM_TRANSPORT_HEADER_TYPE_MASK) == LBM_TRANSPORT_HEADER_TYPE_FINISHER
            ) break;
    }

    LOG_DBG("  Got payload (%d bytes)", ring_buf_size_get(output));

    return 0;
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
    LBM_P2P_TRANSPORT_INIT_PRIORITY, &lora_p2p_transport_api);
