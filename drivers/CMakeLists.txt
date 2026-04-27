# Custom Zephyr funtion that imports the button/ subdirectory if the Kconfig
# option CUSTOM_BUTTON is defined
add_subdirectory_ifdef(CONFIG_DRV8825 drv8825)