# Compilar desde fuente

```sh
sudo apt install -y meson ninja-build g++ pkg-config \
  libgtk-4-dev libadwaita-1-dev libglib2.0-dev
meson setup build --prefix=/usr
ninja -C build
meson test -C build
```

## Tests sin hardware (CI)

```sh
Y520_DRY=1 ./build/tests/test_ec_dry
```

`Y520_DRY=1` simula el EC en memoria (sin `ioperm` ni root) y relaja
polkit en el daemon. El CLI acepta `Y520_CONSERVATION_PATH` y
`Y520_KBD_PATH` para redirigir sysfs en tests.

## Empaquetar .deb

```sh
./packaging/mkdeb.sh [versión]   # sale en packaging/out/
```

Las releases se generan solas al pushear un tag `v*`
(`.github/workflows/release.yml`).
