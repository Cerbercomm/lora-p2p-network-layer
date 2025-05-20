cmake_minimum_required(VERSION 3.20.0)

# Nothing to do if we dont have a designated radio driver
if(${CONFIG_HAS_LBM_P2P_NETWORK_DRIVER})

  # Add library named after containing folder
  zephyr_library()

  # Module include
  zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

  # Code for PHY LoRa layer
  add_subdirectory(drivers)

endif()