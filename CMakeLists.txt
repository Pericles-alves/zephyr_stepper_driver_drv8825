cmake_minimum_required(VERSION 3.20.0)

zephyr_interface_library_named(MODULE_DRV8825_IFACE)

zephyr_include_directories(
  include
)

add_subdirectory(drivers/stepper/drv8825)
