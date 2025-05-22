/*
 * Copyright (c) 2025 Cerbercomm LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Maintained by:
 *   2025-05-20 Or Goshen
 */

#ifndef LORA_P2P_NERWORK_LAYER_H
#define LORA_P2P_NERWORK_LAYER_H

#include "zephyr/toolchain.h"
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC TYPES ------------------------------------------------------------
*/
#define LORA_P2P_BROADCAST_ID 0xFF

struct lora_p2p_network_incoming_t {
	// who is it coming from ?
	uint8_t from;

	// who is it to ?
	uint8_t to;

	// RSSI of the incoming transmission
	int8_t rssi;

	// SNR of the incoming transmission
	int8_t snr;
};

typedef int (*lora_p2p_network_api_int_send_cb)(uint8_t to, const uint8_t *buffer, uint16_t length, void *user_data);
typedef int (*lora_p2p_network_api_int_recv_cb)(uint8_t *buffer, uint16_t length, int8_t *rssi, int8_t *snr, void *user_data);

/**
 * @cond INTERNAL_HIDDEN
 *
 * For internal driver use only, skip these in public documentation.
*/
typedef int (*lora_p2p_network_api_set_callbacks)(const struct device *dev, lora_p2p_network_api_int_send_cb send, lora_p2p_network_api_int_recv_cb recv, void *user_data);
typedef int (*lora_p2p_network_api_set_node_id)(const struct device *dev, uint8_t node_id);
typedef int (*lora_p2p_network_api_send)(const struct device *dev, uint8_t to, const uint8_t *buffer, uint8_t length);
typedef int (*lora_p2p_network_api_recv)(const struct device *dev, struct lora_p2p_network_incoming_t *meta, uint8_t *buffer, uint8_t length);

__subsystem struct lora_p2p_network_driver_api {
	lora_p2p_network_api_set_callbacks set_callbacks;
	lora_p2p_network_api_set_node_id set_node_id;
	lora_p2p_network_api_send send;
	lora_p2p_network_api_recv recv;
};

/** @endcond */

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC DRIVER API -------------------------------------------------------
*/
static inline int lora_p2p_network_set_callbacks(const struct device *dev, lora_p2p_network_api_int_send_cb send, lora_p2p_network_api_int_recv_cb recv, void *user_data) {
	return DEVICE_API_GET(lora_p2p_network, dev)->set_callbacks(dev, send, recv, user_data);
}

static inline int lora_p2p_network_set_node_id(const struct device *dev, uint8_t node_id) {
	return DEVICE_API_GET(lora_p2p_network, dev)->set_node_id(dev, node_id);
}

static inline int lora_p2p_network_send(const struct device *dev, uint8_t to, const uint8_t *buffer, uint8_t length) {
	return DEVICE_API_GET(lora_p2p_network, dev)->send(dev, to, buffer, length);
}

static inline int lora_p2p_network_recv(const struct device *dev, struct lora_p2p_network_incoming_t *meta, uint8_t *buffer, uint8_t length) {
	return DEVICE_API_GET(lora_p2p_network, dev)->recv(dev, meta, buffer, length);
}

#ifdef __cplusplus
}
#endif

#endif  // LORA_P2P_NERWORK_LAYER_H
