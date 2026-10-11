/*
 * Y520EC.cpp — Acceso al Embedded Controller del Lenovo Y520-15IKBN.
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

#include "Y520EC.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <chrono>

#if defined(__linux__) && (defined(__i386__) || defined(__x86_64__))
#include <sys/io.h>
#include <unistd.h>
#define Y520_HAVE_IOPERM 1
#else
#define Y520_HAVE_IOPERM 0
#endif

namespace {

void log_msg(const std::string& msg) {
    std::cerr << "[Y520EC] " << msg << std::endl;
}

std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string read_file_trimmed(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::string line;
    std::getline(f, line);
    return trim(line);
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// --- Estado simulado para modo dry-run (Y520_DRY=1) ---
std::unordered_map<uint8_t, uint8_t>& dry_regs() {
    static std::unordered_map<uint8_t, uint8_t> regs{
        {Y520EC::EXTREME_COOLING_REGISTER, Y520EC::DEACTIVATE},
    };
    return regs;
}

} // namespace

Y520EC::Y520EC() = default;

bool Y520EC::is_dry_run() {
    const char* v = std::getenv("Y520_DRY");
    return v != nullptr && std::string(v) == "1";
}

void Y520EC::initialize() {
    if (initialized_) return;
    if (is_dry_run()) {
        log_msg("dry-run: initialize() simulado, sin ioperm.");
        initialized_ = true;
        return;
    }
#if Y520_HAVE_IOPERM
    if (ioperm(EC_SC, 1, 1) != 0 || ioperm(EC_DATA, 1, 1) != 0) {
        log_msg(std::string("ioperm failed: ") + std::strerror(errno) +
                " (need root / CAP_SYS_RAWIO)");
        throw std::runtime_error(
            "ioperm(0x62/0x66) failed: necesita root o CAP_SYS_RAWIO");
    }
    log_msg("ioperm OK en 0x62 y 0x66.");
    initialized_ = true;
#else
    throw std::runtime_error("ioperm no disponible en esta plataforma");
#endif
}

void Y520EC::ec_wait(uint16_t port, uint8_t flag, uint8_t value) {
    if (is_dry_run()) return;
#if Y520_HAVE_IOPERM
    for (int i = 0; i < 100; ++i) {
        uint8_t data = inb(port);
        if ((data & flag) == value) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "EC timeout esperando port 0x%02X flag 0x%02X == 0x%02X",
                  port, flag, value);
    throw ECTimeout(buf);
#else
    (void)port; (void)flag; (void)value;
    throw std::runtime_error("ec_wait sin ioperm en esta plataforma");
#endif
}

uint8_t Y520EC::ec_read(uint8_t port) {
    if (is_dry_run()) {
        auto& regs = dry_regs();
        auto it = regs.find(port);
        uint8_t v = (it != regs.end()) ? it->second : 0x00;
        log_msg("dry-run: ec_read(0x" + std::to_string(port) + ")");
        return v;
    }
#if Y520_HAVE_IOPERM
    initialize();
    ec_wait(EC_SC, 0x02, 0x00);
    outb(RD_EC, EC_SC);
    ec_wait(EC_SC, 0x02, 0x00);
    outb(port, EC_DATA);
    ec_wait(EC_SC, 0x01, 0x01);
    return inb(EC_DATA);
#else
    (void)port;
    throw std::runtime_error("ec_read sin ioperm en esta plataforma");
#endif
}

void Y520EC::ec_write(uint8_t port, uint8_t val) {
    if (is_dry_run()) {
        dry_regs()[port] = val;
        log_msg("dry-run: ec_write simulado.");
        return;
    }
#if Y520_HAVE_IOPERM
    initialize();
    ec_wait(EC_SC, 0x02, 0x00);
    outb(WR_EC, EC_SC);
    ec_wait(EC_SC, 0x02, 0x00);
    outb(port, EC_DATA);
    ec_wait(EC_SC, 0x02, 0x00);
    outb(val, EC_DATA);
    ec_wait(EC_SC, 0x02, 0x00);
#else
    (void)port; (void)val;
    throw std::runtime_error("ec_write sin ioperm en esta plataforma");
#endif
}

void Y520EC::activate() {
    ec_write(EXTREME_COOLING_REGISTER, ACTIVATE);
    uint8_t back = ec_read(EXTREME_COOLING_REGISTER);
    if (back != ACTIVATE) {
        log_msg("post-escritura: lectura no coincide con 0x40.");
        throw std::runtime_error("Fallo al activar: verificación post-escritura falló");
    }
    log_msg("Extreme Cooling ACTIVADO (0xBD=0x40).");
}

void Y520EC::deactivate() {
    ec_write(EXTREME_COOLING_REGISTER, DEACTIVATE);
    uint8_t back = ec_read(EXTREME_COOLING_REGISTER);
    if (back != DEACTIVATE) {
        log_msg("post-escritura: lectura no coincide con 0x00.");
        throw std::runtime_error("Fallo al desactivar: verificación post-escritura falló");
    }
    log_msg("Extreme Cooling DESACTIVADO (0xBD=0x00).");
}

bool Y520EC::toggle() {
    if (is_active()) {
        deactivate();
        return false;
    }
    activate();
    return true;
}

bool Y520EC::is_active() {
    return ec_read(EXTREME_COOLING_REGISTER) == ACTIVATE;
}

bool Y520EC::detect_supported() {
    const std::string base = "/sys/devices/virtual/dmi/id/";
    const std::string vendor = to_upper(read_file_trimmed(base + "sys_vendor"));
    const std::string product_name = read_file_trimmed(base + "product_name");
    const std::string product_version = read_file_trimmed(base + "product_version");
    const std::string chassis = read_file_trimmed(base + "chassis_type");

    log_msg("DMI sys_vendor='" + vendor + "' product_name='" + product_name +
            "' product_version='" + product_version + "' chassis_type='" + chassis + "'");

    bool dmi_ok = (vendor.find("LENOVO") != std::string::npos) &&
                  (chassis == "9" || chassis == "10" || chassis == "14");

    if (!dmi_ok) {
        log_msg("DMI no coincide (se exige LENOVO + chassis 9/10/14).");
        if (is_dry_run()) {
            log_msg("dry-run: se simula equipo soportado para tests.");
            // Aun así validar el registro simulado.
            uint8_t v = ec_read(EXTREME_COOLING_REGISTER);
            return (v == ACTIVATE || v == DEACTIVATE || v == 0x80);
        }
        return false;
    }

    // DMI OK: exigir lectura de 0xBD en {0x40, 0x00, 0x80}.
    try {
        // En dry-run ec_read nunca lanza; en real requiere ioperm/root.
        if (!is_dry_run()) initialize();
        uint8_t v = ec_read(EXTREME_COOLING_REGISTER);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "lectura 0xBD=0x%02X", v);
        log_msg(buf);
        if (v == ACTIVATE || v == DEACTIVATE || v == 0x80) return true;
        log_msg("valor 0xBD fuera de {0x40,0x00,0x80}: no soportado.");
        return false;
    } catch (const ECTimeout& e) {
        log_msg(std::string("timeout leyendo EC: ") + e.what());
        return false;
    } catch (const std::exception& e) {
        log_msg(std::string("error leyendo EC: ") + e.what());
        return false;
    }
}
