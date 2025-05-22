# direct network specific cmake stuff

# Includes
#zephyr_include_directories()

# Zephyr driver
zephyr_library_sources(
    ${CMAKE_CURRENT_LIST_DIR}/lora_p2p_network_direct.c
)