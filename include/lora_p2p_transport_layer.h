/*
 * Copyright (c) 2025 Cerbercomm LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Maintained by:
 *   2025-07-21 Or Goshen
 */

#ifndef LORA_P2P_TRANSPORT_LAYER_H
#define LORA_P2P_TRANSPORT_LAYER_H

#include "zephyr/toolchain.h"
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/sys/ring_buffer.h>

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC TYPES ------------------------------------------------------------
*/
struct lora_p2p_transport_incoming_t {
	// who is it coming from ?
	uint8_t from;

	// who is it to ?
	uint8_t to;

	// RSSI of the incoming transmission
	int8_t rssi;

	// SNR of the incoming transmission
	int8_t snr;
};

/**
 * @cond INTERNAL_HIDDEN
 *
 * For internal driver use only, skip these in public documentation.
*/
typedef struct device * (*lora_p2p_transport_api_get_network_device)(const struct device *dev);
typedef int (*lora_p2p_transport_api_send)(const struct device *dev, uint8_t to, struct ring_buf *rb, bool reliable);
typedef int (*lora_p2p_transport_api_recv)(const struct device *dev, struct lora_p2p_transport_incoming_t *meta, struct ring_buf *rb);

__subsystem struct lora_p2p_transport_driver_api {
	lora_p2p_transport_api_get_network_device get_network_device;
	lora_p2p_transport_api_send send;
	lora_p2p_transport_api_recv recv;
};

/** @endcond */

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC DRIVER API -------------------------------------------------------
*/
static inline struct device * lora_p2p_transport_get_network_device(const struct device *dev) {
	return DEVICE_API_GET(lora_p2p_transport, dev)->get_network_device(dev);
}

static inline int lora_p2p_transport_send(const struct device *dev, uint8_t to, struct ring_buf *rb, bool reliable) {
	return DEVICE_API_GET(lora_p2p_transport, dev)->send(dev, to, rb, reliable);
}

static inline int lora_p2p_transport_recv(const struct device *dev, struct lora_p2p_transport_incoming_t *meta, struct ring_buf *rb) {
	return DEVICE_API_GET(lora_p2p_transport, dev)->recv(dev, meta, rb);
}

#ifdef __cplusplus
}
#endif

#endif  // LORA_P2P_TRANSPORT_LAYER_H
