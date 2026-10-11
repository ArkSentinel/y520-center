/*
 * Y520EC.h — Acceso al Embedded Controller del Lenovo Y520-15IKBN.
 *
 * Port C++17 de ec4Linux.py (kkYrusobad/ExtremeCoolingy520).
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

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

/// Excepción lanzada cuando ec_wait() agota sus 100 intentos de 1 ms.
class ECTimeout : public std::runtime_error {
public:
    explicit ECTimeout(const std::string& msg) : std::runtime_error(msg) {}
};

/// Excepción lanzada cuando el equipo no es un Y520 soportado.
class ECUnsupported : public std::runtime_error {
public:
    explicit ECUnsupported(const std::string& msg) : std::runtime_error(msg) {}
};

class Y520EC {
public:
    // Puertos / comandos EC (port 1:1 de ec4Linux.py).
    static constexpr uint16_t EC_SC = 0x66;
    static constexpr uint16_t EC_DATA = 0x62;
    static constexpr uint8_t  RD_EC = 0x80;
    static constexpr uint8_t  WR_EC = 0x81;
    static constexpr uint8_t  EXTREME_COOLING_REGISTER = 0xBD;
    static constexpr uint8_t  ACTIVATE = 0x40;
    static constexpr uint8_t  DEACTIVATE = 0x00;

    Y520EC();
    ~Y520EC() = default;

    Y520EC(const Y520EC&) = delete;
    Y520EC& operator=(const Y520EC&) = delete;

    /// Activa el permiso de IO (ioperm). En modo dry-run no hace nada.
    /// Lanza std::runtime_error si ioperm falla (falta root / CAP_SYS_RAWIO).
    void initialize();

    /// Espera hasta que (inb(port) & flag) == value, 100 intentos x 1 ms.
    /// Lanza ECTimeout si expira.
    void ec_wait(uint16_t port, uint8_t flag, uint8_t value);

    /// Lee un byte del registro EC indicado (protocolo RD_EC=0x80).
    uint8_t ec_read(uint8_t port);

    /// Escribe un byte en el registro EC indicado (protocolo WR_EC=0x81).
    void ec_write(uint8_t port, uint8_t val);

    /// Activa Extreme Cooling (escribe 0x40 en 0xBD y verifica por lectura).
    void activate();

    /// Desactiva Extreme Cooling (escribe 0x00 en 0xBD y verifica).
    void deactivate();

    /// Conmuta el estado y devuelve el nuevo estado (true = activo).
    bool toggle();

    /// true si el registro 0xBD contiene 0x40.
    bool is_active();

    /// Verifica DMI (LENOVO + chassis 9/10/14) y que 0xBD ∈ {0x40,0x00,0x80}.
    /// Devuelve true si soportado, false en caso contrario (no lanza,
    /// salvo error grave de IO distinto de "no soportado").
    bool detect_supported();

    /// true cuando la variable de entorno Y520_DRY=1 está definida.
    static bool is_dry_run();

private:
    bool initialized_ = false;
};
