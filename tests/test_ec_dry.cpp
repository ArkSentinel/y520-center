/*
 * test_ec_dry.cpp — Test en seco (Y520_DRY=1) del toggle lógico sin hardware.
 *
 * Compilar: g++ -std=c++17 -DY520_TEST tests/test_ec_dry.cpp src/ec/Y520EC.cpp -o /tmp/test_ec_dry
 * Ejecutar: Y520_DRY=1 /tmp/test_ec_dry
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "../src/ec/Y520EC.h"

#include <cassert>
#include <cstdlib>
#include <iostream>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << " (" << __LINE__ << ")\n"; \
        return 1; \
    } \
    std::cout << "PASS: " << msg << "\n"; \
} while (0)

int main() {
    // Forzar dry-run aunque el entorno no lo defina.
    setenv("Y520_DRY", "1", 1);
    CHECK(Y520EC::is_dry_run(), "dry-run activo via Y520_DRY=1");

    Y520EC ec;

    // initialize() no debe lanzar sin root en dry-run.
    try {
        ec.initialize();
    } catch (const std::exception& e) {
        std::cerr << "FAIL: initialize() lanzó en dry-run: " << e.what() << "\n";
        return 1;
    }
    std::cout << "PASS: initialize() sin IO real\n";

    // ec_wait no debe bloquear ni lanzar en dry-run.
    try {
        ec.ec_wait(Y520EC::EC_SC, 0x02, 0x00);
    } catch (const std::exception& e) {
        std::cerr << "FAIL: ec_wait lanzó en dry-run: " << e.what() << "\n";
        return 1;
    }
    std::cout << "PASS: ec_wait retorna de inmediato en dry-run\n";

    // Estado inicial: desactivado.
    ec.deactivate();
    CHECK(!ec.is_active(), "tras deactivate() is_active()==false");

    // Activar y verificar.
    ec.activate();
    CHECK(ec.is_active(), "tras activate() is_active()==true");
    CHECK(ec.ec_read(Y520EC::EXTREME_COOLING_REGISTER) == Y520EC::ACTIVATE,
          "registro 0xBD==0x40 tras activate()");

    // Toggle lógico: on -> off -> on.
    bool s1 = ec.toggle();
    CHECK(!s1 && !ec.is_active(), "toggle() apaga (on->off)");
    bool s2 = ec.toggle();
    CHECK(s2 && ec.is_active(), "toggle() enciende (off->on)");

    // Escritura/lectura directa de otro registro simulado.
    ec.ec_write(0x10, 0xAB);
    CHECK(ec.ec_read(0x10) == 0xAB, "ec_write/ec_read simétrico en dry-run");

    // detect_supported() debe ser true en dry-run (simula Y520).
    CHECK(ec.detect_supported(), "detect_supported()==true en dry-run");

    std::cout << "ALL DRY TESTS PASSED\n";
    return 0;
}
