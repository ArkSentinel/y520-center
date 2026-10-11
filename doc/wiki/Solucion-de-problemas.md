# Solución de problemas

## La GUI dice "daemon no disponible"

```sh
systemctl status y520d --no-pager
sudo systemctl enable --now y520d
busctl --system list | grep -i lenovo   # debe salir org.lenovo.Y520
```

Si instalaste con prefijo `/usr/local`, reinstala con `--prefix=/usr`
(D-Bus/polkit/systemd no leen `/usr/local`).

## Pide la contraseña dos veces / doble diálogo

Era un bug ya corregido (el refresh revertía el switch y re-disparaba
el Set). Actualiza a la última versión.

## `Unit y520d.service does not exist`

El unit cayó en el dir multiarch (`…/x86_64-linux-gnu/systemd`). Versiones
actuales instalan en `/usr/lib/systemd/system`. Reinstala y:

```sh
sudo systemctl daemon-reload
```

## El Alto del teclado no cambia nada

Normal en este equipo: `max_brightness=1` por sysfs (ver [[Teclado]]).
Usa Fn+Espacio para el Alto del firmware.

## Ver logs del daemon

```sh
journalctl -u y520d --no-pager -n 20 | tail -20
```
