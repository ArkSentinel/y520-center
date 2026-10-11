# Teclado

Retroiluminación blanca del Y520 (3 niveles del firmware, igual que
Fn+Espacio): Off / Bajo / Alto.

## Detalle técnico

- sysfs: `/sys/class/leds/platform::kbd_backlight/brightness`.
- Este Y520 expone `max_brightness=1` por sysfs, así que el kernel
  recorta el nivel 2→1 al escribir: el **Alto por software equivale a
  Encendido**. El Alto real solo lo pone Fn+Espacio en firmware.
- La app ofrece los 3 niveles igual que el teclado físico y lo indica
  en la nota de la pestaña.

## CLI

```sh
y520-cli kbd status
sudo y520-cli kbd 0|1|2
```
