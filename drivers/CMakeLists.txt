# If we're here, we have a designated network driver

# Common includes
zephyr_include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
)

# Common source
#zephyr_library_sources()

# Subdirectories specific to each network driver
add_subdirectory_ifdef(CONFIG_LBM_P2P_NETWORK_DIRECT ${CMAKE_CURRENT_LIST_DIR}/direct)

# Subdirectory for transport layer
add_subdirectory_ifdef(CONFIG_LBM_P2P_TRANSPORT_LAYER ${CMAKE_CURRENT_LIST_DIR}/transport)
