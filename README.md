# esw-zephyr-drv8825-lib

Zephyr module containing a Devicetree binding and a simple library/driver for the texas instruments DRV8825.

## How to add to your app (via west)

In your app’s `west.yml`:

```yaml
manifest:
  projects:
    - name: drv8825
      url: 
      path: modules/drv8825
      revision: main
```
