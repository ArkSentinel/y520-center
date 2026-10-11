# Módulo C-gui: y520-center (GTK4 + libadwaita + gio).
# Habla al daemon por D-Bus; sin daemon usa lectura sysfs + aviso.
# SPDX-License-Identifier: GPL-3.0-or-later

y520_center = executable('y520-center',
  files('y520-center.cpp'),
  dependencies : [gtk4, adwaita, gio, gio_unix],
  install : true,
)
