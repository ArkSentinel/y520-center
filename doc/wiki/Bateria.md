# Batería

El modo conservación detiene la carga en torno al 60 % y alarga la vida
de la batería cuando usas el equipo enchufado.

- sysfs: `/sys/bus/platform/drivers/ideapad_acpi/VPC*/conservation_mode`
  (`1` = activado, `0` = desactivado).
- En la app: pestaña Energía → Modo conservación.
- CLI:

```sh
y520-cli conservation status
sudo y520-cli conservation on|off
```
