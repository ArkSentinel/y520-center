# Uso

## Fan Control

Réplica del ExtremeCooling4Linux original: título, logo, interruptor
**Enable Extreme Cooling** y validación post-escritura. Al activar, los
ventiladores van al 100 %. Ver [[Extreme-Cooling]].

## Energía

- **Modo conservación**: limita la carga (~60 %) vía
  `ideapad_acpi/conservation_mode`. Ver [[Bateria]].
- Estado de batería y perfil de `power-profiles-daemon`.

## Teclado

Combo Off / Bajo / Alto (niveles del firmware, Fn+Espacio hace lo mismo).
Ver [[Teclado]].

## System

- 9 filas DMI (Model / Bios / Board).
- Temperaturas en vivo (barra + °C por sensor, escala según `temp_max/crit`).
- Uso por núcleo en vivo (barra + %, refresco cada 2 s).

## Autorización

Una sola acción polkit (`org.lenovo.Y520.manage`, `auth_admin_keep`):
la primera vez pide la contraseña y vale para enfriamiento, batería y
teclado durante la sesión.
