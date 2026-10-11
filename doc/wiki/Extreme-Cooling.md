# Extreme Cooling

Modo especial de ventiladores del Legion Y520 (los pone al 100 %),
portado del protocolo de [ExtremeCooling4Linux](https://github.com/kkYrusobad/ExtremeCoolingy520):

- EC por puertos IO `0x66/0x62`, comandos `0x80/0x81`.
- Registro `0xBD`: `0x40` = activado, `0x00` = desactivado.
- Requiere root / `CAP_SYS_RAWIO` (`ioperm`); en la app solo lo toca
  el daemon `y520d`, nunca la GUI.

## CLI

```sh
sudo y520-cli check    # ¿equipo soportado? (LENOVO + chasis + 0xBD válido)
sudo y520-cli enable | disable | toggle | status
```

## Advertencia

Sin garantía. Úsalo en periodos cortos: al 100 % los ventiladores se
desgastan antes. No lo uses en equipos sin soporte de Lenovo.
