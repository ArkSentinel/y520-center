# Y520 Center — Catastro y Plan Maestro (Ubuntu 26.04 + GNOME 50 + C++)

> Archivo catastro: sirve como registro único del proyecto. Última actualización: 2026-10-11.
> Equipo diana: LENOVO / Lenovo Y520-15IKBN / 80WK (verificado vía DMI en esta máquina).
> Stack exigido: librerías nativas GNOME (GTK4 + libadwaita + GDBus/gio) y C++ si es posible.

## 1. Catastro hardware verificado (esta máquina)

| Ítem | Valor probado | Comando | Conclusión |
|------|---------------|---------|------------|
| sys_vendor | `LENOVO` | `cat /sys/devices/virtual/dmi/id/sys_vendor` | OK Nerve |
| product_version | `80WK` | `cat .../product_version` | Y520-15IKBN |
| product_name | `Lenovo Y520-15IKBN` | `cat .../product_name` | diana exacta |
| kbd_backlight | `platform::kbd_backlight` existe | `ls /sys/class/leds/` | Blanco 2 niveles vía ideapad_acpi, NO USB RGB |
| USB ITE 048d:c963/c965 | ausente | `lsusb \| grep 048d` | Repo keyboard-led NO aplicable directo, solo fallback |
| conservation_mode | `VPC2004:00`, valor `1` | `cat /sys/bus/platform/drivers/ideapad_acpi/VPC*/conservation_mode` | Soportado |
| hwmon | hwmon0-4 | `ls /sys/class/hwmon` | Sensores disponibles |
| OS | Ubuntu 26.04.1, GNOME Shell 50.1, g++ 15.2.0, GTK 4.22, Adwaita 1.9 (runtime) | `lsb_release`, `gnome-shell --version` | Falta instalar `-dev` + meson/ninja |

## 2. Análisis repos (qué se reutiliza)

### 2.1 kkYrusobad/ExtremeCoolingy520 (base crítica, portar 1:1 a C++)
- `ec4Linux.py`: EC por IO ports `EC_SC=0x66`, `EC_DATA=0x62`, `RD_EC=0x80`, `WR_EC=0x81`, registro `0xBD`, `0x40=on / 0x00=off`.
- `ec_wait(port,flag,val)`: loop 100×1ms sobre `inb`, timeout → excepción.
- `inicializate()`: `ioperm(0x62,1,1)` + `ioperm(0x66,1,1)`, requiere root/CAP_SYS_RAWIO.
- Detección: LENOVO + chassis 9/10/14 + lectura 0xBD ∈ {0x40,0x00,0x80}.
- CLI: `enable/disable/change-state/status/check`. GUI GTK3 + `exec4Linux.sh` (pkexec). Licencia GPL-3.0 → este proyecto debe ser GPL-3.0.

### 2.2 InstinctEx/lenovo-ideapad-legion-keyboard-led (NO directo Y520)
- USB `048d:c963`, `ctrl_transfer(0x21,0x09,0x03CC)` con payload 32 bytes `[cc,16,effect,speed,brightness,4×RGB,...]`.
- En Y520 no hay ese USB → implementar `KbdBackend` con prioridad: (1) `/sys/class/leds/platform::kbd_backlight/brightness` (0/1/2), (2) USB ITE si aparece en otros Y520, (3) solo UI informativa. + regla udev + documentar.

### 2.3 johnfanv2/LenovoLegionLinux (arquitectura, no driver)
- Y520 2017 EC ≠ 8227, sin tabla fan-curve ni WMI powermode → NO portar `legion-laptop.c`.
- Reutilizar: idea `legion_cli` + GUI Qt→aquí Adw, `hwmon` estándar, `fan_unlock` como análogo a ExtremeCooling, `conservation_mode`, perfiles vía `power-profiles-daemon`.

### 2.4 cszach/linux-on-lenovo-legion (scripts aplicables)
- Batería: `echo 1/0 > .../VPC2004*/conservation_mode`.
- GPU híbrida: `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia`, `DRI_PRIME=1`.
- Brillo: `amdgpu.backlight=0`, fallback xrandr. Huella: ignorar (Y520 no tiene).

## 3. Arquitectura objetivo (nativa GNOME + C++)

```
y520-center/
  PLAN-Y520-NERVE-CENTER.md      # este catastro
  meson.build                    # raíz (gtk4, adwaita, gio, polkit-gobject)
  src/ec/Y520EC.{h,cpp}          # ec_wait/read/write, activate/deactivate/is_active/detect (sys/io.h)
  src/cli/y520-cli.cpp           # enable|disable|toggle|status|check + conservation + kbd
  src/daemon/y520d.cpp           # D-Bus system org.lenovo.Y520, polkit check, systemd service
  src/gui/y520-center.cpp        # AdwApplicationWindow: Enfriamiento/Energía/Teclado/Sistema
  data/org.lenovo.Y520.policy    # polkit
  data/org.lenovo.Y520.conf      # dbus system
  data/y520d.service             # systemd system
  data/10-y520-kbd.rules         # udev (ITE si existe)
  data/org.lenovo.Y520Center.metainfo.xml
  debian/ (fase 2)
```

D-Bus `org.lenovo.Y520` (/org/lenovo/Y520):
- `SetExtremeCooling(b on)`, `GetExtremeCooling()→b`, `ToggleExtremeCooling()→b`
- `SetConservation(b)`, `GetConservation()→b`
- `SetKbdBacklight(u level 0-2)`, `GetKbdBacklight()→u`
- `GetSystemInfo()→a{ss}` (dmi + bios + gpu)
- Señal `ExtremeCoolingChanged(b)`

Seguridad: daemon root con `CAP_SYS_RAWIO`, GUI sin privilegios, `polkit auth_admin_keep`, `ioperm` solo en daemon. Validar lectura post-escritura. Advertir uso prolongado ventiladores 100%.

## 4. Reparto en subagentes paralelos

- **A-ec-core**: `src/ec/*`, `src/cli/*`, `tests/test_ec_dry.cpp` (modo dry sin IO para CI). Owner exclusivo esos paths.
- **B-daemon**: `src/daemon/*`, `data/*.policy`, `data/*.conf`, `data/*.service`. Depende de interfaz Y520EC (usa stub hasta que A termine, no editar src/ec).
- **C-gui**: `src/gui/*`, `data/*.metainfo.xml`, `.desktop`. Habla a D-Bus vía gio, mock si daemon ausente. No tocar daemon/ec.
- **D-build**: `meson.build`, `meson.options`, `README.md`, `data/10-y520-kbd.rules`, verificación `meson setup build && ninja`. Solo crea/actualiza esos + instala deps dev vía apt.

Orden: A+B+C en paralelo, D integra al final. Todos GPL-3.0 headers.

## 5. Dependencias Ubuntu 26.04 (para D)

```
sudo apt update && sudo apt install -y meson ninja-build g++ pkg-config \
 libgtk-4-dev libadwaita-1-dev libpolkit-gobject-1-dev libglib2.0-dev \
 lm-sensors power-profiles-daemon
```

## 6. Verificación en Y520 (criterios aceptación)

1. `sudo ./build/src/cli/y520-cli check` → detecta LENOVO 80WK, lee 0xBD sin excepción.
2. `y520-cli status` ↔ switch GUI sincronizados; `toggle` cambia 0x40↔0x00 y re-lee igual.
3. `cat /sys/class/leds/platform::kbd_backlight/max_brightness` ≥1; slider GUI escribe brightness 0-2.
4. `conservation_mode` lee/escribe 0/1 desde GUI+CLI.
5. `sensors` muestra CPU/GPU en página Sistema; `meson test` en dry-mode pasa sin root.
6. Sin regresión Wayland/GNOME 50, sin python portio/gi.

## 7. Límites explícitos (no prometer)

- Sin curvas 10 puntos ni RPM por nivel (EC Y520 no las tiene, solo on/off 100%).
- Sin RGB 4 zonas salvo que `lsusb 048d` aparezca; Y520 verificado = blanco.
- Sin overclock CPU/GPU en v1; solo perfiles vía PPD + info NVIDIA offload.

## 8. Estado (módulo D-build, 2026-10-11)

Archivos reales verificados con `ls -R`:

```
y520/:
  PLAN-Y520-NERVE-CENTER.md
  README.md                    # nuevo (D)
  meson.build                  # nuevo (D, raíz)
  data/
    10-y520-kbd.rules          # nuevo (D, udev ITE fallback)
    org.lenovo.Y520.conf       # existe (B)
    org.lenovo.Y520.policy     # existe (B)
    org.lenovo.Y520Center.desktop      # existe (C)
    org.lenovo.Y520Center.metainfo.xml # existe (C)
    y520d.service              # existe (B)
  src/
    cli/meson.build + y520-cli.cpp     # meson nuevo (D), fuente existe (A)
    daemon/meson.build + y520d.cpp     # meson nuevo (D), fuente existe (B)
    ec/meson.build + Y520EC.{h,cpp}    # meson nuevo (D), fuentes existen (A)
    gui/meson.build + y520-center.cpp  # meson nuevo (D), fuente existe (C)
  tests/meson.build + test_ec_dry.cpp  # meson nuevo (D), fuente existe (A)
```

No se modificó ningún archivo preexistente (A/B/C); D solo creó
`meson.build` (raíz + 5 submódulos), `data/10-y520-kbd.rules` y `README.md`.

Pendientes (no se intentó sudo/apt en esta sesión):

```
sudo apt update && sudo apt install -y meson ninja-build g++ pkg-config \
  libgtk-4-dev libadwaita-1-dev libpolkit-gobject-1-dev libglib2.0-dev \
  lm-sensors power-profiles-daemon
meson setup build && ninja -C build
```

Verificación `meson setup build --wipe` (2026-10-11): FALLA como se esperaba —
`meson: orden no encontrada` (ni meson ni las `-dev` están instalados).
Anotado: reintentar tras instalar dependencias.

## 9. Verificación post-instalación dev (2026-10-11, Ubuntu 26.04.1, GNOME 50.1)

- `meson setup build --wipe`: OK (gtk4 4.22.4, adwaita 1.9.1, gio 2.88.0, gcc 15.2.0; systemd/udev con fallback).
- Fixes GUI aplicados: `adw_preferences_page_add(page, g)` sin `GTK_WIDGET()`, `show_error` con `AdwDialog*`, `adw_action_row_set_subtitle` en SwitchRows.
- `ninja -C build`: OK, 3 binarios (`y520-cli` 580K, `y520d`, `y520-center`).
- `meson test`: 1/1 OK (ec dry).
- `Y520_DRY=1 y520-cli status/check`: OK en 80WK (DMI LENOVO/80WK/Y520-15IKBN/chassis 10, 0xBD=0x00).
- Sin root real: falla limpio con exit 2 + mensaje ioperm/CAP_SYS_RAWIO (esperado).
- `desktop-file-validate`: OK, metainfo XML: OK.
- Pendiente: `sudo ./build/src/cli/y520-cli check` con root real + instalar (`meson install`) + probar GUI/daemon.
- HW real con sudo (2026-10-11, reportado por usuario): `ioperm OK en 0x62 y 0x66`, `lectura 0xBD=0x00`, `Equipo soportado`. Acceso EC directo funcional en 80WK.
- Rediseño GUI réplica del Glade original (2026-10-11): ventana `ExtremeCooling4Linux` no redimensionable, SwitchSwitcher centrado + separadores, páginas `Fan Control` (título + logo SVG + `Enable Extreme Cooling` + Switch), `System` (9 filas Model/Bios/Board), `Readme` (texto original), + `Energía`/`Teclado` extra, botón About con créditos originales v0.3. Iconos encontrados (no generados): `weather-windy/battery-good/input-keyboard/computer/help-contents-symbolic` + SVG legion de LenovoLegionLinux en `data/icons/`. Daemon instalado OK con prefijo `/usr`, servicio activo.
- Fix GUI (2026-10-11): `daemon_system_info` usaba `GVariantIter` en pila con `g_variant_get(..., &iter)` → CRITICAL `is_valid_iter`; cambiado a `GVariantIter*` heap como exige la API. Verificado: compila, `meson test` 1/1.
- Fix ventana Wayland (2026-10-11): sin HeaderBar no hay botones CSD (cerrar/min/max) y el stack estiraba la ventana. Añadido `AdwHeaderBar`, `resizable=TRUE`, stack dentro de `GtkScrolledWindow`. Escrituras D-Bus (Set/Toggle) con timeout 60 s en vez de 5 s (el prompt polkit superaba los 5 s y el botón parecía no funcionar).
- Fix daemon polkit (2026-10-11): `CheckAuthorization` exige `((sa{sv})sa{ss}us)` con doble paréntesis; el builder usaba `(sa{sv}sa{ss}us)` → `InvalidArgs` y denegaba todo Set/Toggle. Corregido el `g_variant_builder_init`. Verificado: compila, `meson test` 1/1.
- Fix doble prompt/diálogo (2026-10-11): al fallar un Set, el refresh revertía el switch y ese cambio programático re-disparaba `notify` → segundo Set (doble contraseña + doble error). Los tres refreshes ahora blindan el cambio programático con `*_changing`. Verificado: compila, `meson test` 1/1.
- Fix respuesta polkit (2026-10-11): este polkitd devuelve `((bba{ss}))` en vez de `(bba{ss})`; llamada ahora con `reply_type NULL` y parseo defensivo de ambas formas. Verificado: compila, `meson test` 1/1.
- Contraseña única + GUI vistosa (2026-10-11): acción polkit única `org.lenovo.Y520.manage` con `auth_admin_keep` (un solo prompt para cooling/batería/teclado); ventana 620x800 con scroll; sección `CPU (uso por núcleo)` con `GtkLevelBar` + % en vivo cada 2 s desde `/proc/stat`; temperaturas también en vivo (barra por sensor + °C, escala según `temp_max/crit`); About solo con detalles + licencia GPL (sin créditos). Verificado: compila, `meson test` 1/1.
- Teclado según máximo real (2026-10-11): este Y520 tiene `max_brightness=1` (solo Off/On, sin "Alto"; Fn+Espacio lo confirma a nivel firmware). Combo y CLI ahora usan 0–máx. detectado (`Off/Encendido` aquí) en vez de 0–2 fijos. Verificado: `kbd status` → `(0-1)`, `kbd 2` rechazado.
- Teclado 3 niveles restaurados (2026-10-11, a petición): Off/Bajo/Alto como el firmware; nota en UI que por sysfs el Alto equivale a Encendido (máx 1). CLI acepta 0–2 con aviso si recorta. Verificado: compila, `meson test` 1/1.
- Repo en GitHub (2026-10-11): https://github.com/ArkSentinel/y520-center — renombrado a **Y520 Center** (visibles + paquete `y520-center`; IDs internos `org.lenovo.Y520*` intactos), identidad ArkSentinel + noreply, rama `main` + tag `v0.1.0` pusheados. CI y Release .deb en curso.
- Release v0.1.0 publicada con `y520-center_0.1.0_amd64.deb`. About del repo (descripción + topics) listo. Wiki versionada en `doc/wiki/` pendiente de aprovisionar (crear 1.ª página en web). README con icono + sección Pantallazos pendiente de 3 PNGs del usuario.
