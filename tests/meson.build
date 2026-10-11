# Módulo A-ec-core: test en seco sin hardware/root (Y520_DRY=1).
# SPDX-License-Identifier: GPL-3.0-or-later

test_ec_dry = executable('test_ec_dry',
  y520_ec_sources + files('test_ec_dry.cpp'),
  include_directories : y520_ec_inc,
  install : false,
)

# El propio test hace setenv("Y520_DRY","1"), pero se fija también el
# entorno aquí para que `meson test` sea seco aunque cambie el test.
test('ec dry (Y520_DRY=1)', test_ec_dry, env : ['Y520_DRY=1'])
