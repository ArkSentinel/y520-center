# Y520 Center — build raíz (módulo D-build).
# SPDX-License-Identifier: GPL-3.0-or-later

project('y520-center', 'cpp',
  version : '0.1.0',
  license : 'GPL-3.0-or-later',
  default_options : ['cpp_std=c++17', 'warning_level=2'],
)

cxx = meson.get_compiler('cpp')

# Dependencias nativas GNOME exigidas por el plan §1/§5.
gtk4     = dependency('gtk4', required : true)
adwaita  = dependency('libadwaita-1', required : true)
gio      = dependency('gio-2.0', required : true)
gio_unix = dependency('gio-unix-2.0', required : true)

# Directorios de instalación del sistema (con fallback si no hay .pc).
# Nota: en Ubuntu el libdir es multiarch (…/x86_64-linux-gnu) y systemd/udev
# NO leen de ahí; el fallback usa prefix/lib/… (/usr/lib/…).
systemd_dep = dependency('systemd', required : false)
if systemd_dep.found()
  systemunitdir = systemd_dep.get_variable(pkgconfig : 'systemdsystemunitdir')
else
  systemunitdir = get_option('prefix') / 'lib' / 'systemd' / 'system'
endif

udev_dep = dependency('udev', required : false)
if udev_dep.found()
  udevdir = udev_dep.get_variable(pkgconfig : 'udevdir')
else
  udevdir = get_option('prefix') / 'lib' / 'udev'
endif
udevrulesdir = udevdir / 'rules.d'

# Módulos: A-ec-core (ec+cli+tests), B-daemon, C-gui.
subdir('src/ec')
subdir('src/cli')
subdir('src/daemon')
subdir('src/gui')
subdir('tests')

# --- data: polkit / D-Bus / systemd / udev / desktop ---

install_data('data/org.lenovo.Y520.policy',
  install_dir : get_option('datadir') / 'polkit-1' / 'actions',
)

install_data('data/org.lenovo.Y520.conf',
  install_dir : get_option('datadir') / 'dbus-1' / 'system.d',
)

install_data('data/y520d.service',
  install_dir : systemunitdir,
)

install_data('data/10-y520-kbd.rules',
  install_dir : udevrulesdir,
)

install_data('data/org.lenovo.Y520Center.desktop',
  install_dir : get_option('datadir') / 'applications',
)

install_data('data/icons/org.lenovo.Y520Center.svg',
  install_dir : get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)

install_data('data/org.lenovo.Y520Center.metainfo.xml',
  install_dir : get_option('datadir') / 'metainfo',
)
