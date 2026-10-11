# Módulo B-daemon: y520d (D-Bus system org.lenovo.Y520, gio).
# Sin dependencia de src/ec: lógica EC mínima duplicada en y520d.cpp.
# SPDX-License-Identifier: GPL-3.0-or-later

y520d = executable('y520d',
  files('y520d.cpp'),
  dependencies : [gio, gio_unix],
  install : true,
)
