# esw-zephyr-drv8825-lib

Zephyr module containing a Devicetree binding and a simple library/driver for the Texas Instruments DRV8825 stepper motor driver.

This project was inspired by the Zephyr DRV84XX stepper driver implementation and bindings, especially the
[ti,drv84xx Devicetree binding documentation](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/stepper/ti/drv84xx.c),
and it uses the Zephyr Stepper API subsystem. (https://docs.zephyrproject.org/latest/hardware/peripherals/stepper/index.html)

## How to add to your app (via west)

In your app’s `west.yml`:

```yaml
manifest:
  projects:
    - name: drv8825
      url: git@github.com:Pericles-alves/zephyr_stepper_driver_drv8825.git
      path: modules/drv8825
      revision: main