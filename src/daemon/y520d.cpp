// SPDX-License-Identifier: GPL-3.0-or-later
// y520d — daemon de sistema Lenovo Y520 (D-Bus org.lenovo.Y520).
// Sin dependencias de src/ec ni src/gui: lógica EC mínima duplicada aquí.

#include <gio/gio.h>
#include <glib.h>

#include <sys/io.h>
#include <unistd.h>

#include <glob.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

// ---------------------------------------------------------------- EC local ---
// Constantes portadas 1:1 de ec4Linux.py (ExtremeCoolingy520, GPL-3.0).
namespace ec {
constexpr uint16_t EC_SC = 0x66;
constexpr uint16_t EC_DATA = 0x62;
constexpr uint8_t RD_EC = 0x80;
constexpr uint8_t WR_EC = 0x81;
constexpr uint8_t REG = 0xBD;
constexpr uint8_t VAL_ON = 0x40;
constexpr uint8_t VAL_OFF = 0x00;

static bool dry_mode() {
    const char* v = g_getenv("Y520_DRY");
    return v && (v[0] == '1') && (v[1] == '\0');
}

// Estado virtual para tests/CI (Y520_DRY=1): sin ioperm/inb/outb.
static uint8_t s_dry_reg = VAL_OFF;
static bool s_io_ok = false;

static bool is_dry() { return dry_mode(); }

static bool io_init() {
    if (is_dry())
        return true;
    if (s_io_ok)
        return true;
    if (ioperm(EC_DATA, 1, 1) != 0)
        return false;
    if (ioperm(EC_SC, 1, 1) != 0)
        return false;
    s_io_ok = true;
    return true;
}

static bool ec_wait(uint16_t port, uint8_t flag, uint8_t val) {
    for (int i = 0; i < 100; ++i) {
        uint8_t d = inb(port);
        if (((d >> flag) & 0x1) == val)
            return true;
        usleep(1000);
    }
    return false;
}

static bool ec_read(uint8_t reg, uint8_t& out) {
    if (is_dry()) {
        out = (reg == REG) ? s_dry_reg : 0;
        return true;
    }
    if (!io_init())
        return false;
    if (!ec_wait(EC_SC, 1, 0))
        return false;
    outb(RD_EC, EC_SC);
    if (!ec_wait(EC_SC, 1, 0))
        return false;
    outb(reg, EC_DATA);
    if (!ec_wait(EC_SC, 0, 1))
        return false;
    out = inb(EC_DATA);
    return true;
}

static bool ec_write(uint8_t reg, uint8_t val) {
    if (is_dry()) {
        if (reg == REG)
            s_dry_reg = val;
        return true;
    }
    if (!io_init())
        return false;
    if (!ec_wait(EC_SC, 1, 0))
        return false;
    outb(WR_EC, EC_SC);
    if (!ec_wait(EC_SC, 1, 0))
        return false;
    outb(reg, EC_DATA);
    if (!ec_wait(EC_SC, 1, 0))
        return false;
    outb(val, EC_DATA);
    if (!ec_wait(EC_SC, 1, 0))
        return false;
    return true;
}

static bool activate() { return ec_write(REG, VAL_ON); }
static bool deactivate() { return ec_write(REG, VAL_OFF); }

static bool is_active(bool& out) {
    uint8_t v = 0;
    if (!ec_read(REG, v))
        return false;
    out = (v == VAL_ON);
    return true;
}
} // namespace ec

// --------------------------------------------------------------- sysfs -------
static const char* CONSERVATION_GLOB =
    "/sys/bus/platform/drivers/ideapad_acpi/VPC*/conservation_mode";
static const char* KBD_BRIGHTNESS =
    "/sys/class/leds/platform::kbd_backlight/brightness";
static const char* KBD_MAX =
    "/sys/class/leds/platform::kbd_backlight/max_brightness";

static std::string find_conservation_path() {
    glob_t g{};
    std::string ret;
    if (glob(CONSERVATION_GLOB, GLOB_NOSORT, nullptr, &g) == 0 && g.gl_pathc > 0)
        ret = g.gl_pathv[0];
    globfree(&g);
    return ret;
}

static bool read_int_file(const char* path, long& out) {
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    char buf[64]{};
    bool ok = fgets(buf, sizeof buf, f) != nullptr;
    fclose(f);
    if (!ok)
        return false;
    out = strtol(buf, nullptr, 10);
    return true;
}

static bool write_int_file(const char* path, long val) {
    FILE* f = fopen(path, "w");
    if (!f)
        return false;
    fprintf(f, "%ld\n", val);
    fclose(f);
    return true;
}

static std::string read_trim_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f)
        return "";
    char buf[256]{};
    std::string s;
    if (fgets(buf, sizeof buf, f))
        s = buf;
    fclose(f);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

// ---------------------------------------------------------------- polkit ----
// Acción única: una sola autenticación (auth_admin_keep) vale para
// enfriamiento, batería y teclado.
static const char* ACTION_MANAGE = "org.lenovo.Y520.manage";

static bool polkit_check(GDBusConnection* sys_conn, const char* sender,
                         const char* action_id) {
    if (!sys_conn || !sender)
        return false;
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("((sa{sv})sa{ss}us)"));
    g_variant_builder_open(&b, G_VARIANT_TYPE("(sa{sv})"));
    g_variant_builder_add(&b, "s", "system-bus-name");
    g_variant_builder_open(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "name", g_variant_new_string(sender));
    g_variant_builder_close(&b);
    g_variant_builder_close(&b);
    g_variant_builder_add(&b, "s", action_id);
    g_variant_builder_open(&b, G_VARIANT_TYPE("a{ss}"));
    g_variant_builder_close(&b);
    g_variant_builder_add(&b, "u", (guint32)1); // AllowUserInteraction
    g_variant_builder_add(&b, "s", "");
    GVariant* params = g_variant_builder_end(&b);

    GError* err = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        sys_conn, "org.freedesktop.PolicyKit1",
        "/org/freedesktop/PolicyKit1/Authority",
        "org.freedesktop.PolicyKit1.Authority", "CheckAuthorization", params,
        nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &err);
    if (!res) {
        g_log("y520d", G_LOG_LEVEL_WARNING,
              "polkit CheckAuthorization falló (%s): %s — %s", action_id,
              err ? err->message : "?",
              ec::is_dry() ? "Y520_DRY=1, permitiendo para tests"
                           : "denegando");
        if (err)
            g_error_free(err);
        return ec::is_dry(); // en CI/tests sin polkit, permitir solo en dry
    }
    gboolean authorized = FALSE, challenge = FALSE;
    if (g_variant_is_of_type(res, G_VARIANT_TYPE("(bba{ss})"))) {
        g_variant_get(res, "(bb@a{ss})", &authorized, &challenge, nullptr);
    } else if (g_variant_is_of_type(res, G_VARIANT_TYPE("((bba{ss}))"))) {
        GVariant* inner = g_variant_get_child_value(res, 0);
        if (inner) {
            g_variant_get(inner, "(bb@a{ss})", &authorized, &challenge, nullptr);
            g_variant_unref(inner);
        }
    } else {
        g_log("y520d", G_LOG_LEVEL_WARNING,
              "polkit devolvió tipo inesperado %s para %s — denegando",
              g_variant_get_type_string(res), action_id);
        g_variant_unref(res);
        return false;
    }
    g_variant_unref(res);
    if (!authorized)
        g_log("y520d", G_LOG_LEVEL_WARNING,
              "polkit denegó %s para %s", action_id, sender);
    return authorized == TRUE;
}

// ---------------------------------------------------------------- D-Bus -----
static const char* INTROSPECTION_XML =
    "<node>"
    " <interface name='org.lenovo.Y520'>"
    "  <method name='SetExtremeCooling'>"
    "   <arg name='on' type='b' direction='in'/>"
    "  </method>"
    "  <method name='GetExtremeCooling'>"
    "   <arg name='active' type='b' direction='out'/>"
    "  </method>"
    "  <method name='ToggleExtremeCooling'>"
    "   <arg name='active' type='b' direction='out'/>"
    "  </method>"
    "  <method name='SetConservation'>"
    "   <arg name='on' type='b' direction='in'/>"
    "  </method>"
    "  <method name='GetConservation'>"
    "   <arg name='active' type='b' direction='out'/>"
    "  </method>"
    "  <method name='SetKbdBacklight'>"
    "   <arg name='level' type='u' direction='in'/>"
    "  </method>"
    "  <method name='GetKbdBacklight'>"
    "   <arg name='level' type='u' direction='out'/>"
    "  </method>"
    "  <method name='GetSystemInfo'>"
    "   <arg name='info' type='a{ss}' direction='out'/>"
    "  </method>"
    "  <signal name='ExtremeCoolingChanged'>"
    "   <arg name='active' type='b'/>"
    "  </signal>"
    " </interface>"
    "</node>";

static GDBusConnection* g_conn = nullptr;

static void emit_cooling_changed(bool active) {
    if (!g_conn)
        return;
    GError* err = nullptr;
    g_dbus_connection_emit_signal(g_conn, nullptr, "/org/lenovo/Y520",
                                  "org.lenovo.Y520", "ExtremeCoolingChanged",
                                  g_variant_new("(b)", (gboolean)active), &err);
    if (err) {
        g_log("y520d", G_LOG_LEVEL_WARNING, "no se pudo emitir señal: %s",
              err->message);
        g_error_free(err);
    }
}

static void on_method(GDBusConnection* conn, const char* sender,
                      const char* /*obj*/, const char* /*iface*/,
                      const char* method, GVariant* params,
                      GDBusMethodInvocation* inv, void* /*ud*/) {
    if (g_strcmp0(method, "GetExtremeCooling") == 0) {
        bool active = false;
        if (!ec::is_active(active)) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_FAILED,
                                                  "No se pudo leer EC 0xBD");
            return;
        }
        g_dbus_method_invocation_return_value(inv,
                                              g_variant_new("(b)", (gboolean)active));
        return;
    }
    if (g_strcmp0(method, "SetExtremeCooling") == 0) {
        gboolean on = FALSE;
        g_variant_get(params, "(b)", &on);
        if (!polkit_check(conn, sender, ACTION_MANAGE)) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_ACCESS_DENIED,
                                                  "No autorizado por polkit");
            return;
        }
        bool ok = on ? ec::activate() : ec::deactivate();
        if (!ok) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_FAILED,
                                                  "Escritura EC 0xBD falló");
            return;
        }
        bool verify = false; // validar lectura post-escritura
        if (!ec::is_active(verify) || verify != (bool)on) {
            g_dbus_method_invocation_return_error(
                inv, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "Verificación post-escritura falló");
            return;
        }
        g_log("y520d", G_LOG_LEVEL_INFO, "ExtremeCooling -> %s (por %s)",
              on ? "on" : "off", sender);
        emit_cooling_changed(verify);
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }
    if (g_strcmp0(method, "ToggleExtremeCooling") == 0) {
        if (!polkit_check(conn, sender, ACTION_MANAGE)) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_ACCESS_DENIED,
                                                  "No autorizado por polkit");
            return;
        }
        bool active = false;
        if (!ec::is_active(active)) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_FAILED,
                                                  "No se pudo leer EC 0xBD");
            return;
        }
        bool target = !active;
        if (!(target ? ec::activate() : ec::deactivate())) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_FAILED,
                                                  "Escritura EC 0xBD falló");
            return;
        }
        bool verify = false;
        if (!ec::is_active(verify) || verify != target) {
            g_dbus_method_invocation_return_error(
                inv, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "Verificación post-escritura falló");
            return;
        }
        g_log("y520d", G_LOG_LEVEL_INFO, "ExtremeCooling toggle -> %s (por %s)",
              verify ? "on" : "off", sender);
        emit_cooling_changed(verify);
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(b)", (gboolean)verify));
        return;
    }
    if (g_strcmp0(method, "GetConservation") == 0) {
        std::string p = find_conservation_path();
        if (p.empty()) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_FAILED,
                                                  "conservation_mode no encontrado");
            return;
        }
        long v = 0;
        if (!read_int_file(p.c_str(), v)) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_FAILED,
                                                  "No se pudo leer conservation_mode");
            return;
        }
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(b)", (gboolean)(v != 0)));
        return;
    }
    if (g_strcmp0(method, "SetConservation") == 0) {
        gboolean on = FALSE;
        g_variant_get(params, "(b)", &on);
        if (!polkit_check(conn, sender, ACTION_MANAGE)) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_ACCESS_DENIED,
                                                  "No autorizado por polkit");
            return;
        }
        std::string p = find_conservation_path();
        if (p.empty()) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_FAILED,
                                                  "conservation_mode no encontrado");
            return;
        }
        if (!write_int_file(p.c_str(), on ? 1 : 0)) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_FAILED,
                                                  "No se pudo escribir conservation_mode");
            return;
        }
        g_log("y520d", G_LOG_LEVEL_INFO, "Conservation -> %s (por %s)",
              on ? "on" : "off", sender);
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }
    if (g_strcmp0(method, "GetKbdBacklight") == 0) {
        long v = 0;
        if (!read_int_file(KBD_BRIGHTNESS, v))
            v = 0;
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(u)", (guint32)v));
        return;
    }
    if (g_strcmp0(method, "SetKbdBacklight") == 0) {
        guint32 level = 0;
        g_variant_get(params, "(u)", &level);
        if (!polkit_check(conn, sender, ACTION_MANAGE)) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_ACCESS_DENIED,
                                                  "No autorizado por polkit");
            return;
        }
        long maxv = 2;
        read_int_file(KBD_MAX, maxv); // si falta, asume 2
        if ((long)level > maxv)
            level = (guint32)maxv;
        if (!write_int_file(KBD_BRIGHTNESS, (long)level)) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_FAILED,
                                                  "No se pudo escribir kbd_backlight");
            return;
        }
        g_log("y520d", G_LOG_LEVEL_INFO, "KbdBacklight -> %u (por %s)", level,
              sender);
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }
    if (g_strcmp0(method, "GetSystemInfo") == 0) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a{ss}"));
        auto add = [&](const char* k, const std::string& v) {
            g_variant_builder_add(&b, "{ss}", k, v.c_str());
        };
        add("sys_vendor",
            read_trim_file("/sys/devices/virtual/dmi/id/sys_vendor"));
        add("product_name",
            read_trim_file("/sys/devices/virtual/dmi/id/product_name"));
        add("product_version",
            read_trim_file("/sys/devices/virtual/dmi/id/product_version"));
        add("bios_version",
            read_trim_file("/sys/devices/virtual/dmi/id/bios_version"));
        add("bios_date",
            read_trim_file("/sys/devices/virtual/dmi/id/bios_date"));
        {
            std::string p = find_conservation_path();
            long v = -1;
            if (!p.empty())
                read_int_file(p.c_str(), v);
            add("conservation_mode", v < 0 ? "unknown" : std::to_string(v));
        }
        {
            long v = 0, m = 0;
            read_int_file(KBD_BRIGHTNESS, v);
            read_int_file(KBD_MAX, m);
            add("kbd_backlight", std::to_string(v));
            add("kbd_max", std::to_string(m));
        }
        GVariant* dict = g_variant_builder_end(&b);
        g_dbus_method_invocation_return_value(
            inv, g_variant_new_tuple(&dict, 1));
        return;
    }
    g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "Método desconocido: %s", method);
}

static const GDBusInterfaceVTable kVtable = {on_method, nullptr, nullptr, {}};

static void on_bus_acquired(GDBusConnection* conn, const char* /*name*/,
                            void* /*ud*/) {
    GError* err = nullptr;
    GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(INTROSPECTION_XML, &err);
    if (!info) {
        g_log("y520d", G_LOG_LEVEL_CRITICAL, "introspección inválida: %s",
              err ? err->message : "?");
        if (err)
            g_error_free(err);
        return;
    }
    guint id = g_dbus_connection_register_object(
        conn, "/org/lenovo/Y520", info->interfaces[0], &kVtable, nullptr,
        nullptr, &err);
    g_dbus_node_info_unref(info);
    if (id == 0) {
        g_log("y520d", G_LOG_LEVEL_CRITICAL, "register_object falló: %s",
              err ? err->message : "?");
        if (err)
            g_error_free(err);
        return;
    }
    g_conn = conn;
    g_log("y520d", G_LOG_LEVEL_INFO, "objeto /org/lenovo/Y520 registrado");
}

static void on_name_lost(GDBusConnection* conn, const char* /*name*/,
                         void* loop_ptr) {
    (void)conn;
    g_log("y520d", G_LOG_LEVEL_CRITICAL,
          "se perdió el nombre org.lenovo.Y520, saliendo");
    g_main_loop_quit(static_cast<GMainLoop*>(loop_ptr));
}

int main() {
    g_log_set_handler("y520d", G_LOG_LEVEL_MASK,
                      [](const char* domain, GLogLevelFlags level,
                         const char* msg, void*) {
                          const char* tag = "INFO";
                          if (level & G_LOG_LEVEL_WARNING)
                              tag = "WARN";
                          else if (level & G_LOG_LEVEL_CRITICAL)
                              tag = "CRIT";
                          else if (level & G_LOG_LEVEL_DEBUG)
                              tag = "DEBUG";
                          g_printerr("[y520d][%s] %s\n", tag, msg ? msg : "");
                          (void)domain;
                      },
                      nullptr);

    if (!ec::io_init() && !ec::is_dry()) {
        g_log("y520d", G_LOG_LEVEL_WARNING,
              "ioperm(0x62/0x66) falló: se requiere root/CAP_SYS_RAWIO");
    }
    if (ec::is_dry())
        g_log("y520d", G_LOG_LEVEL_INFO, "modo Y520_DRY=1 (sin IO real)");

    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    guint owner = g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.lenovo.Y520",
                                 G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired,
                                 nullptr, on_name_lost, loop, nullptr);
    g_log("y520d", G_LOG_LEVEL_INFO, "daemon iniciado, esperando bus de sistema");
    g_main_loop_run(loop);
    g_bus_unown_name(owner);
    g_main_loop_unref(loop);
    return 0;
}
