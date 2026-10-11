# Instalación

## Opción A — .deb (recomendada)

Descarga el `.deb` de la [Release](https://github.com/ArkSentinel/y520-center/releases):

```sh
sudo dpkg -i y520-center_<versión>_amd64.deb
sudo systemctl enable --now y520d
```

El postinst recarga systemd/udev y la caché de iconos solo.

## Opción B — desde fuente (Ubuntu 26.04)

```sh
sudo apt install -y meson ninja-build g++ pkg-config \
  libgtk-4-dev libadwaita-1-dev libglib2.0-dev lm-sensors
git clone https://github.com/ArkSentinel/y520-center.git
cd y520-center
meson setup build --prefix=/usr
ninja -C build
sudo meson install -C build
sudo systemctl daemon-reload
sudo systemctl enable --now y520d
```

> `--prefix=/usr` es obligatorio: D-Bus, polkit y systemd no leen rutas
> bajo `/usr/local` y el daemon no se registraría.

## Verificar

```sh
systemctl status y520d --no-pager
sudo y520-cli check      # ioperm OK + lectura 0xBD en un 80WK real
y520-center
```
