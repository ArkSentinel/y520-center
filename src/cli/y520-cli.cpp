/*
 * y520-cli.cpp — CLI del Lenovo Y520 Nerve Center (módulo A-ec-core).
 *
 * Uso:
 *   y520-cli {enable|disable|toggle|status|check}
 *   y520-cli conservation {on|off|status}
 *   y520-cli kbd {nivel|status}
 *
 * Códigos de retorno: 0 ok, 1 error, 2 no soportado.
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

#include "../ec/Y520EC.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

// ES + EN mínimos en cada mensaje de usuario.

void usage() {
    std::cout <<
        "Uso / Usage:\n"
        "  y520-cli {enable|disable|toggle|status|check}\n"
        "  y520-cli conservation {on|off|status}\n"
        "  y520-cli kbd {nivel|status}\n";
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string read_file(const std::string& path, bool& ok) {
    std::ifstream f(path);
    if (!f.is_open()) { ok = false; return ""; }
    std::string line;
    std::getline(f, line);
    ok = true;
    return trim(line);
}

bool write_file(const std::string& path, const std::string& value) {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << value;
    return static_cast<bool>(f);
}

// Permite a los tests redirigir sysfs sin root:
//   Y520_CONSERVATION_PATH=/tmp/fake_conservation
//   Y520_KBD_PATH=/tmp/fake_kbd_brightness
std::string find_conservation_path() {
    if (const char* ov = std::getenv("Y520_CONSERVATION_PATH")) {
        if (std::string(ov).size() > 0) return ov;
    }
    const std::string dir = "/sys/bus/platform/drivers/ideapad_acpi";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return "";
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        std::string name = e.path().filename().string();
        if (name.rfind("VPC", 0) == 0) {
            std::string p = e.path().string() + "/conservation_mode";
            if (fs::exists(p, ec)) return p;
        }
    }
    return "";
}

std::string kbd_path() {
    if (const char* ov = std::getenv("Y520_KBD_PATH")) {
        if (std::string(ov).size() > 0) return ov;
    }
    return "/sys/class/leds/platform::kbd_backlight/brightness";
}

int cmd_conservation(const std::string& sub) {
    std::string path = find_conservation_path();
    if (path.empty()) {
        std::cerr << "conservation_mode no encontrado / not found.\n";
        return 2;
    }
    if (sub == "status") {
        bool ok = false;
        std::string v = read_file(path, ok);
        if (!ok) {
            std::cerr << "Error leyendo / failed to read " << path << "\n";
            return 1;
        }
        if (v == "1")
            std::cout << "Conservación activada / Conservation ON (1)\n";
        else
            std::cout << "Conservación desactivada / Conservation OFF (0)\n";
        return 0;
    }
    if (sub == "on" || sub == "off") {
        std::string v = (sub == "on") ? "1" : "0";
        if (!write_file(path, v)) {
            std::cerr << "Error escribiendo / failed to write " << path
                      << " (¿root? / need root?)\n";
            return 1;
        }
        std::cout << (sub == "on"
            ? "Conservación activada / Conservation ON\n"
            : "Conservación desactivada / Conservation OFF\n");
        return 0;
    }
    usage();
    return 1;
}

int cmd_kbd(const std::string& sub) {
    std::string path = kbd_path();
    std::string maxp = path;
    const std::string suf = "/brightness";
    if (maxp.size() > suf.size() &&
        maxp.compare(maxp.size() - suf.size(), suf.size(), suf) == 0)
        maxp.replace(maxp.size() - suf.size(), suf.size(), "/max_brightness");
    int mx = 2;
    { bool ok = false; std::string mv = read_file(maxp, ok);
      if (ok) { try { int m = std::stoi(mv); if (m >= 1 && m <= 4) mx = m; } catch (...) {} } }
    if (sub == "status") {
        bool ok = false;
        std::string v = read_file(path, ok);
        if (!ok) {
            std::cerr << "Error leyendo / failed to read " << path << "\n";
            return 1;
        }
        std::cout << "Teclado / Keyboard backlight: " << v << " (0-" << mx << ")\n";
        return 0;
    }
    try {
        int lvl = std::stoi(sub);
        if (lvl < 0 || lvl > 2) { usage(); return 1; }
        if (lvl > mx)
            std::cerr << "Nota: max_brightness=" << mx << " por sysfs; el kernel lo deja en "
                      << mx << " (el Alto real lo pone Fn+Espacio en firmware).\n";
        if (!write_file(path, std::to_string(lvl))) {
            std::cerr << "Error escribiendo / failed to write " << path
                      << " (¿root? / need root?)\n";
            return 1;
        }
        std::cout << "Teclado ajustado a / Keyboard set to " << lvl << "\n";
        return 0;
    } catch (...) { usage(); return 1; }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];

    if (cmd == "conservation") {
        if (argc < 3) { usage(); return 1; }
        return cmd_conservation(argv[2]);
    }
    if (cmd == "kbd") {
        if (argc < 3) { usage(); return 1; }
        return cmd_kbd(argv[2]);
    }

    try {
        Y520EC ec;

        if (cmd == "check") {
            bool ok = ec.detect_supported();
            if (ok) {
                std::cout << "Equipo soportado / System supported.\n";
                return 0;
            }
            std::cout << "Equipo NO soportado / System NOT supported.\n";
            return 2;
        }

        if (cmd == "status") {
            if (!ec.detect_supported()) {
                std::cout << "Equipo NO soportado / System NOT supported.\n";
                return 2;
            }
            bool active = ec.is_active();
            std::cout << (active
                ? "Extreme Cooling: ACTIVADO / ON\n"
                : "Extreme Cooling: DESACTIVADO / OFF\n");
            return 0;
        }

        if (cmd == "enable") {
            if (!ec.detect_supported()) {
                std::cerr << "Equipo NO soportado / System NOT supported.\n";
                return 2;
            }
            ec.activate();
            std::cout << "Extreme Cooling activado / enabled.\n";
            return 0;
        }
        if (cmd == "disable") {
            if (!ec.detect_supported()) {
                std::cerr << "Equipo NO soportado / System NOT supported.\n";
                return 2;
            }
            ec.deactivate();
            std::cout << "Extreme Cooling desactivado / disabled.\n";
            return 0;
        }
        if (cmd == "toggle") {
            if (!ec.detect_supported()) {
                std::cerr << "Equipo NO soportado / System NOT supported.\n";
                return 2;
            }
            bool now = ec.toggle();
            std::cout << (now
                ? "Extreme Cooling activado / enabled.\n"
                : "Extreme Cooling desactivado / disabled.\n");
            return 0;
        }

        usage();
        return 1;
    } catch (const ECUnsupported& e) {
        std::cerr << "No soportado / Unsupported: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
