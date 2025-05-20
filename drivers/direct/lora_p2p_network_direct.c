#include "lora_p2p_network_direct.h"
#include "lora_p2p_network_layer.h"

#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

/* Driver init
*/
static int lora_p2p_network_direct_init(const struct device *dev) {
    //return -ENOTSUP;
    return 0;
}

/* Driver API
*/

/* Driver & Device definition
*/
static const lora_p2p_network_direct_config_t config = {

};

static DEVICE_API(lora_p2p_network, lora_p2p_network_api) = {
};

DEVICE_DEFINE(lora_p2p_network_direct, LORA_P2P_NETWORK_DIRECT_DRIVER_NAME, &lora_p2p_network_direct_init,
    NULL, NULL, NULL, POST_KERNEL,
    CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &lora_p2p_network_api);
