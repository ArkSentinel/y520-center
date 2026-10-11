// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Y520 Nerve Center — GUI GNOME nativa (módulo C-gui)
 * Copyright (C) 2026  Y520 Nerve Center contributors
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
 *
 * Módulo C-gui (PLAN-Y520-NERVE-CENTER.md §3-§4):
 *   AdwApplication org.lenovo.Y520Center con AdwViewStack de 4 páginas
 *   (Enfriamiento / Energía / Teclado / Sistema).
 *   Habla al daemon por D-Bus sistema org.lenovo.Y520 (/org/lenovo/Y520).
 *   Fallback: sin bus/daemon → sysfs directo en modo lectura + aviso
 *   "daemon no disponible". La GUI nunca usa ioperm/EC directo.
 */

#include <adwaita.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

/* ------------------------------------------------------------------ */
/* Constantes D-Bus / sysfs                                            */
/* ------------------------------------------------------------------ */

namespace {
constexpr const char *kAppId = "org.lenovo.Y520Center";
constexpr const char *kBusName = "org.lenovo.Y520";
constexpr const char *kBusPath = "/org/lenovo/Y520";
constexpr const char *kBusIface = "org.lenovo.Y520";
constexpr const char *kDaemonMissingMsg =
    "daemon no disponible — mostrando solo lectura sysfs directa";
constexpr const char *kKbdNote =
    "3 niveles como Fn+Espacio (firmware). Por sysfs este Y520 expone máx 1: Alto por software equivale a Encendido.";
// Texto informativo original de ExtremeCooling4Linux (id_information_text,
// mainui.glade, GPL-3.0, Alberto Vicente aka Odin).
constexpr const char *kReadmeText =
    "Extreme cooling is a special fan mode supported\n"
    "in Lenovo Legion Yoga laptops. This mode can\n"
    "increase the cooling performance of your laptop\n"
    "and reduces its temperature. This feature is\n"
    "supported only by Lenovo through its Windows\n"
    "program Lenovo Sense Nerve.\n"
    "\n"
    "ExtremeCooling4Linux has been created to enable\n"
    "and support this special feature on Linux. Currently\n"
    "is only tested in Lenovo Legion Y520, but it should\n"
    "be work in other models like Lenovo Legion Y720.\n"
    "\n"
    "However this program does not have warranty\n"
    "and only use at your own risk. Enable extreme\n"
    "cooling only in short periods of time as it can\n"
    "reduce the lifespan of your fans and do not\n"
    "use in laptops without extreme cooling support\n"
    "from Lenovo.";
constexpr const char *kAboutProgram = "ExtremeCooling4Linux";
constexpr const char *kAboutVersion = "0.3";
constexpr const char *kAboutComments =
    "A simple utility to enable Extreme Cooling fan mode for Lenovo Legion laptops";
constexpr const char *kAboutWebsite = "https://gitlab.com/OdinTdh/extremecooling4linux";
// Iconos del sistema encontrados (no generados): Adwaita + legion SVG propio.
constexpr const char *kIconFan = "weather-windy-symbolic";
constexpr const char *kIconPower = "battery-good-symbolic";
constexpr const char *kIconKbd = "input-keyboard-symbolic";
constexpr const char *kIconComputer = "computer-symbolic";
constexpr const char *kIconReadme = "help-contents-symbolic";
// Logo: SVG encontrado (LenovoLegionLinux, GPL-2.0, johnfanv2) instalado como
// icono hicolor de la app; rutas de búsqueda en orden de preferencia.
constexpr const char *kLogoPaths[] = {
    "/usr/share/icons/hicolor/scalable/apps/org.lenovo.Y520Center.svg",
    "/usr/local/share/icons/hicolor/scalable/apps/org.lenovo.Y520Center.svg",
    "data/icons/org.lenovo.Y520Center.svg",
    nullptr};
} // namespace

/* Declaración adelantada: read_dmi (arriba) usa read_sysfs_file (abajo). */
static std::string read_sysfs_file(const std::string &path);

/* Lee un campo DMI de /sys/devices/virtual/dmi/id (página System original). */
static std::string read_dmi(const char *field) {
  return read_sysfs_file(std::string("/sys/devices/virtual/dmi/id/") + field);
}

/* ------------------------------------------------------------------ */
/* Utilidades sysfs                                                    */
/* ------------------------------------------------------------------ */

static std::string trim_str(std::string s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                                  [](unsigned char c) { return !std::isspace(c); }));
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](unsigned char c) { return !std::isspace(c); })
              .base(),
          s.end());
  return s;
}

static std::string read_sysfs_file(const std::string &path) {
  std::ifstream f(path);
  if (!f.good())
    return {};
  std::string line;
  std::getline(f, line);
  return trim_str(line);
}

/* Busca el primer conservation_mode bajo drivers ideapad_acpi. */
static std::string conservation_mode_path() {
  static const char *cands[] = {
      "/sys/bus/platform/drivers/ideapad_acpi/VPC2004:00/conservation_mode",
      nullptr};
  for (int i = 0; cands[i]; ++i) {
    std::ifstream f(cands[i]);
    if (f.good())
      return cands[i];
  }
  // Barrido genérico VPC* como fallback.
  DIR *d = opendir("/sys/bus/platform/drivers/ideapad_acpi");
  if (!d)
    return {};
  std::string found;
  while (dirent *e = readdir(d)) {
    if (strncmp(e->d_name, "VPC", 3) != 0)
      continue;
    std::string p = std::string("/sys/bus/platform/drivers/ideapad_acpi/") +
                    e->d_name + "/conservation_mode";
    std::ifstream f(p);
    if (f.good()) {
      found = p;
      break;
    }
  }
  closedir(d);
  return found;
}

static std::string kbd_brightness_path() {
  const char *p = "/sys/class/leds/platform::kbd_backlight/brightness";
  std::ifstream f(p);
  return f.good() ? p : std::string();
}

/* El firmware (Fn+Espacio) maneja 3 niveles: Off/Bajo/Alto (0/1/2).
 * OJO: este Y520 expone max_brightness=1 por sysfs, así que el kernel
 * recorta el 2→1 al escribir; el Alto real solo lo pone el firmware.
 * La UI ofrece los 3 niveles igual que el teclado físico. */
static const char *kbd_level_label(int i) {
  if (i == 0)
    return "Off";
  if (i == 1)
    return "Bajo";
  return "Alto";
}

static std::string battery_capacity_sysfs() {
  // Primera batería que exponga capacity.
  DIR *d = opendir("/sys/class/power_supply");
  if (!d)
    return {};
  std::string val;
  while (dirent *e = readdir(d)) {
    if (e->d_name[0] == '.')
      continue;
    std::string p = std::string("/sys/class/power_supply/") + e->d_name + "/capacity";
    std::string v = read_sysfs_file(p);
    if (!v.empty()) {
      val = v;
      break;
    }
  }
  closedir(d);
  return val;
}

static std::string battery_status_sysfs() {
  DIR *d = opendir("/sys/class/power_supply");
  if (!d)
    return {};
  std::string val;
  while (dirent *e = readdir(d)) {
    if (e->d_name[0] == '.')
      continue;
    std::string type =
        read_sysfs_file(std::string("/sys/class/power_supply/") + e->d_name + "/type");
    if (type != "Battery")
      continue;
    val = read_sysfs_file(std::string("/sys/class/power_supply/") + e->d_name + "/status");
    break;
  }
  closedir(d);
  return val;
}

/* Sensores hwmon descubiertos una vez: ruta input + etiqueta + máximo. */
struct TempSensor {
  std::string input_path;
  std::string title; // p. ej. "coretemp (Package id 0)"
  double max_c = 105.0;
};

static std::vector<TempSensor> discover_temp_sensors() {
  std::vector<TempSensor> out;
  DIR *d = opendir("/sys/class/hwmon");
  if (!d)
    return out;
  while (dirent *e = readdir(d)) {
    if (e->d_name[0] == '.')
      continue;
    std::string base = std::string("/sys/class/hwmon/") + e->d_name;
    std::string name = read_sysfs_file(base + "/name");
    if (name.empty())
      name = e->d_name;
    for (int i = 1; i <= 8; ++i) {
      char input[32];
      snprintf(input, sizeof(input), "/temp%d_input", i);
      if (read_sysfs_file(base + input).empty())
        continue;
      char lbl[32];
      snprintf(lbl, sizeof(lbl), "/temp%d_label", i);
      std::string lab = read_sysfs_file(base + lbl);
      TempSensor s;
      s.input_path = base + input;
      s.title = lab.empty() ? name : (name + " (" + lab + ")");
      char mx[32];
      snprintf(mx, sizeof(mx), "/temp%d_max", i);
      std::string smax = read_sysfs_file(base + mx);
      if (smax.empty()) {
        snprintf(mx, sizeof(mx), "/temp%d_crit", i);
        smax = read_sysfs_file(base + mx);
      }
      try {
        if (!smax.empty())
          s.max_c = std::stod(smax) / 1000.0;
      } catch (...) {
      }
      if (s.max_c <= 0)
        s.max_c = 105.0;
      out.push_back(s);
    }
  }
  closedir(d);
  return out;
}

static double read_temp_c(const std::string &input_path) {
  try {
    return std::stod(read_sysfs_file(input_path)) / 1000.0;
  } catch (...) {
    return -1000.0; // ilegible
  }
}

/* ------------------------------------------------------------------ */
/* Estado de la app                                                    */
/* ------------------------------------------------------------------ */

struct AppState {
  AdwApplication *app = nullptr;
  GDBusProxy *proxy = nullptr; // daemon org.lenovo.Y520 (system bus), o nullptr
  bool daemon_available = false;

  // Widgets que hay que refrescar ante cambios (réplica del Glade original:
  // GtkSwitch id_enable_extreme_cooling_switch + ListBox de 9 filas DMI).
  GtkSwitch *cool_switch = nullptr;
  GtkLabel *cool_state_label = nullptr;
  GtkSwitch *cons_switch = nullptr;
  GtkLabel *ppd_label = nullptr;
  GtkLabel *batt_label = nullptr;
  AdwComboRow *kbd_combo = nullptr;
  // Temperaturas en vivo: una fila por sensor con barra + °C.
  struct TempWidgets {
    GtkLevelBar *bar = nullptr;
    GtkLabel *val = nullptr;
    TempSensor sensor;
  };
  std::vector<TempWidgets> temp_rows;
  // Monitor de núcleos: una fila por CPU con barra de nivel + porcentaje.
  struct CpuCoreWidgets {
    GtkLevelBar *bar = nullptr;
    GtkLabel *pct = nullptr;
  };
  std::vector<CpuCoreWidgets> cpu_rows;
  std::vector<unsigned long long> cpu_prev_idle;
  std::vector<unsigned long long> cpu_prev_total;
  // Valores DMI de la página System (réplica id_list_box_product/bios/board).
  GtkLabel *dmi_product_name = nullptr;
  GtkLabel *dmi_product_version = nullptr;
  GtkLabel *dmi_sys_vendor = nullptr;
  GtkLabel *dmi_bios_version = nullptr;
  GtkLabel *dmi_bios_date = nullptr;
  GtkLabel *dmi_bios_vendor = nullptr;
  GtkLabel *dmi_board_name = nullptr;
  GtkLabel *dmi_board_version = nullptr;
  GtkLabel *dmi_board_vendor = nullptr;
  AdwBanner *banner = nullptr;
  bool cool_changing = false;
  bool cons_changing = false;
  bool kbd_changing = false;
};

/* Muestra de /proc/stat por núcleo: idle y total acumulados. */
static void cpu_stat_sample(std::vector<unsigned long long> &idle_out,
                            std::vector<unsigned long long> &total_out) {
  idle_out.clear();
  total_out.clear();
  std::ifstream f("/proc/stat");
  if (!f.good())
    return;
  std::string line;
  while (std::getline(f, line)) {
    if (line.compare(0, 3, "cpu") != 0 || line.size() < 4 ||
        !std::isdigit(static_cast<unsigned char>(line[3])))
      continue; // solo cpu0..cpuN, no la agregada "cpu "
    std::istringstream is(line.substr(4));
    unsigned long long v, total = 0, idle = 0;
    int col = 0;
    while (is >> v) {
      total += v;
      if (col == 3 || col == 4)
        idle += v; // idle + iowait
      ++col;
    }
    idle_out.push_back(idle);
    total_out.push_back(total);
  }
}

/* % de uso por núcleo entre la muestra previa y la actual (0.0–100.0). */
static std::vector<double> cpu_usage_pct(AppState *st) {
  std::vector<unsigned long long> idle, total;
  cpu_stat_sample(idle, total);
  std::vector<double> pct;
  if (st->cpu_prev_total.size() != total.size()) {
    st->cpu_prev_idle = idle;
    st->cpu_prev_total = total;
    pct.assign(total.size(), 0.0);
    return pct;
  }
  for (size_t i = 0; i < total.size(); ++i) {
    unsigned long long dt = total[i] - st->cpu_prev_total[i];
    unsigned long long di = idle[i] - st->cpu_prev_idle[i];
    pct.push_back(dt == 0 ? 0.0 : (100.0 * (dt - di)) / dt);
  }
  st->cpu_prev_idle = idle;
  st->cpu_prev_total = total;
  return pct;
}

static gboolean sensors_tick(gpointer data) {
  AppState *st = static_cast<AppState *>(data);
  std::vector<double> pct = cpu_usage_pct(st);
  for (size_t i = 0; i < st->cpu_rows.size() && i < pct.size(); ++i) {
    double v = pct[i] < 0 ? 0 : (pct[i] > 100 ? 100 : pct[i]);
    if (st->cpu_rows[i].bar)
      gtk_level_bar_set_value(st->cpu_rows[i].bar, v / 100.0);
    if (st->cpu_rows[i].pct) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%.0f %%", v);
      gtk_label_set_text(st->cpu_rows[i].pct, buf);
    }
  }
  for (auto &t : st->temp_rows) {
    double c = read_temp_c(t.sensor.input_path);
    if (c < -999.0) {
      if (t.val)
        gtk_label_set_text(t.val, "—");
      continue;
    }
    if (c < 0)
      c = 0;
    if (t.bar)
      gtk_level_bar_set_value(t.bar, c / t.sensor.max_c);
    if (t.val) {
      char buf[24];
      snprintf(buf, sizeof(buf), "%.1f °C", c);
      gtk_label_set_text(t.val, buf);
    }
  }
  return TRUE; // repetir cada intervalo
}

/* ------------------------------------------------------------------ */
/* D-Bus org.lenovo.Y520 (daemon)                                      */
/* ------------------------------------------------------------------ */

static void app_set_daemon_available(AppState *st, bool avail) {
  st->daemon_available = avail;
  if (st->banner)
    adw_banner_set_revealed(st->banner, !avail ? TRUE : FALSE);
}

static GVariant *proxy_call_sync(AppState *st, const char *method, GVariant *params,
                                 GError **error) {
  if (!st->proxy) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "%s", kDaemonMissingMsg);
    return nullptr;
  }
  return g_dbus_proxy_call_sync(st->proxy, method, params, G_DBUS_CALL_FLAGS_NONE, 5000,
                                nullptr, error);
}

/* Escrituras Set/Toggle: 60 s porque el agente polkit pide contraseña
 * y el usuario puede tardar más de 5 s; con 5 s la llamada expiraba y el
 * botón parecía "no funcionar". */
static GVariant *proxy_call_set(AppState *st, const char *method, GVariant *params,
                                GError **error) {
  if (!st->proxy) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "%s", kDaemonMissingMsg);
    return nullptr;
  }
  return g_dbus_proxy_call_sync(st->proxy, method, params, G_DBUS_CALL_FLAGS_NONE, 60000,
                                nullptr, error);
}

static void connect_daemon(AppState *st) {
  GError *err = nullptr;
  st->proxy = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, nullptr, kBusName, kBusPath, kBusIface,
      nullptr, &err);
  if (!st->proxy || err) {
    if (err)
      g_error_free(err);
    if (st->proxy) {
      g_object_unref(st->proxy);
      st->proxy = nullptr;
    }
    app_set_daemon_available(st, false);
    return;
  }
  // Sondeo: si GetExtremeCooling falla, el daemon no responde → fallback lectura.
  GError *perr = nullptr;
  GVariant *r = proxy_call_sync(st, "GetExtremeCooling", nullptr, &perr);
  if (r)
    g_variant_unref(r);
  if (perr)
    g_error_free(perr);
  app_set_daemon_available(st, r != nullptr);
}

/* Getters con fallback sysfs (solo lectura). Devuelven -1 si desconocido. */
static int get_extreme_cooling(AppState *st) {
  GError *err = nullptr;
  GVariant *r = proxy_call_sync(st, "GetExtremeCooling", nullptr, &err);
  if (err)
    g_error_free(err);
  if (r) {
    gboolean b = FALSE;
    g_variant_get(r, "(b)", &b);
    g_variant_unref(r);
    return b ? 1 : 0;
  }
  // Sin daemon no se puede leer el EC (requiere ioperm/root): desconocido.
  return -1;
}

static int get_conservation(AppState *st) {
  GError *err = nullptr;
  GVariant *r = proxy_call_sync(st, "GetConservation", nullptr, &err);
  if (err)
    g_error_free(err);
  if (r) {
    gboolean b = FALSE;
    g_variant_get(r, "(b)", &b);
    g_variant_unref(r);
    return b ? 1 : 0;
  }
  std::string p = conservation_mode_path();
  if (p.empty())
    return -1;
  std::string v = read_sysfs_file(p);
  if (v == "1")
    return 1;
  if (v == "0")
    return 0;
  return -1;
}

static int get_kbd_backlight(AppState *st) {
  GError *err = nullptr;
  GVariant *r = proxy_call_sync(st, "GetKbdBacklight", nullptr, &err);
  if (err)
    g_error_free(err);
  if (r) {
    guint32 lvl = 0;
    g_variant_get(r, "(u)", &lvl);
    g_variant_unref(r);
    return static_cast<int>(lvl);
  }
  std::string p = kbd_brightness_path();
  if (p.empty())
    return -1;
  try {
    return std::stoi(read_sysfs_file(p));
  } catch (...) {
    return -1;
  }
}

/* power-profiles-daemon: propiedad ActiveProfile (solo informativo). */
static std::string get_ppd_profile() {
  GError *err = nullptr;
  GDBusProxy *ppd = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, nullptr, "net.hadess.PowerProfiles",
      "/net/hadess/PowerProfiles", "org.freedesktop.DBus.Properties", nullptr, &err);
  if (!ppd || err) {
    if (err)
      g_error_free(err);
    if (ppd)
      g_object_unref(ppd);
    return {};
  }
  GError *cerr = nullptr;
  GVariant *r = g_dbus_proxy_call_sync(
      ppd, "Get",
      g_variant_new("(ss)", "net.hadess.PowerProfiles", "ActiveProfile"),
      G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &cerr);
  g_object_unref(ppd);
  if (cerr)
    g_error_free(cerr);
  if (!r)
    return {};
  GVariant *inner = nullptr;
  g_variant_get(r, "(v)", &inner);
  std::string profile;
  if (inner) {
    profile = g_variant_get_string(inner, nullptr);
    g_variant_unref(inner);
  }
  g_variant_unref(r);
  return profile;
}

/* ------------------------------------------------------------------ */
/* Diálogo de error                                                    */
/* ------------------------------------------------------------------ */

static void show_error(GtkWindow *parent, const char *title, const char *msg) {
  AdwDialog *dlg = ADW_DIALOG(adw_alert_dialog_new(title, msg));
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "ok", "Aceptar");
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dlg), "ok");
  adw_dialog_present(dlg, GTK_WIDGET(parent));
}

/* ------------------------------------------------------------------ */
/* Página Enfriamiento                                                 */
/* ------------------------------------------------------------------ */

static void cool_refresh(AppState *st) {
  int v = get_extreme_cooling(st);
  // El cambio programático del switch NO debe re-disparar notify::active
  // (provocaba un segundo Set → doble prompt polkit + doble diálogo).
  st->cool_changing = true;
  if (st->cool_switch) {
    if (v < 0) {
      // Desconocido sin daemon: no forzar el switch; la etiqueta lo explica.
      gtk_widget_set_sensitive(GTK_WIDGET(st->cool_switch), st->daemon_available);
    } else {
      gboolean want = v ? TRUE : FALSE;
      if (gtk_switch_get_active(st->cool_switch) != want)
        gtk_switch_set_active(st->cool_switch, want);
      gtk_widget_set_sensitive(GTK_WIDGET(st->cool_switch), st->daemon_available);
    }
  }
  st->cool_changing = false;
  if (st->cool_state_label) {
    const char *t = (v < 0) ? "Estado: desconocido (se requiere daemon)"
                            : (v ? "Estado: Extreme Cooling ACTIVADO (ventiladores 100 %)"
                                 : "Estado: Extreme Cooling desactivado");
    gtk_label_set_text(st->cool_state_label, t);
  }
}

static void on_cool_switch_notify(GObject *obj, GParamSpec *, gpointer data) {
  AppState *st = static_cast<AppState *>(data);
  if (st->cool_changing)
    return;
  gboolean on = gtk_switch_get_active(GTK_SWITCH(obj));
  if (!st->daemon_available) {
    cool_refresh(st);
    return;
  }
  st->cool_changing = true;
  GError *err = nullptr;
  GVariant *r = proxy_call_set(st, "SetExtremeCooling",
                                g_variant_new("(b)", on ? TRUE : FALSE), &err);
  if (r)
    g_variant_unref(r);
  if (err) {
    GtkWindow *win =
        GTK_WINDOW(gtk_application_get_active_window(GTK_APPLICATION(st->app)));
    show_error(win, "No se pudo cambiar el modo",
               err->message ? err->message : "Error D-Bus desconocido");
    g_error_free(err);
  } else {
    // Validación post-escritura: re-leer y sincronizar.
    int v = get_extreme_cooling(st);
    if (v >= 0 && st->cool_switch &&
        gtk_switch_get_active(st->cool_switch) != (v ? TRUE : FALSE))
      gtk_switch_set_active(st->cool_switch, v ? TRUE : FALSE);
  }
  st->cool_changing = false;
  cool_refresh(st);
}

static void on_cool_toggle_clicked(GtkButton *, gpointer data) {
  AppState *st = static_cast<AppState *>(data);
  if (!st->daemon_available) {
    GtkWindow *win =
        GTK_WINDOW(gtk_application_get_active_window(GTK_APPLICATION(st->app)));
    show_error(win, "Daemon no disponible", kDaemonMissingMsg);
    return;
  }
  GError *err = nullptr;
  GVariant *r = proxy_call_set(st, "ToggleExtremeCooling", nullptr, &err);
  gboolean now = FALSE;
  if (r) {
    g_variant_get(r, "(b)", &now);
    g_variant_unref(r);
  }
  if (err) {
    GtkWindow *win =
        GTK_WINDOW(gtk_application_get_active_window(GTK_APPLICATION(st->app)));
    show_error(win, "No se pudo alternar", err->message ? err->message : "Error D-Bus");
    g_error_free(err);
  } else if (st->cool_switch && !st->cool_changing) {
    st->cool_changing = true;
    gtk_switch_set_active(st->cool_switch, now);
    st->cool_changing = false;
  }
  cool_refresh(st);
}

/* Señal ExtremeCoolingChanged(b) del daemon → refrescar switch. */
static void on_daemon_signal(GDBusProxy *, const gchar *, const gchar *signal,
                             GVariant *params, gpointer data) {
  AppState *st = static_cast<AppState *>(data);
  if (g_strcmp0(signal, "ExtremeCoolingChanged") == 0) {
    gboolean b = FALSE;
    g_variant_get(params, "(b)", &b);
    if (st->cool_switch && !st->cool_changing) {
      st->cool_changing = true;
      gtk_switch_set_active(st->cool_switch, b);
      st->cool_changing = false;
    }
    cool_refresh(st);
  }
}

/* ------------------------------------------------------------------ */
/* Página Energía                                                      */
/* ------------------------------------------------------------------ */

static void energy_refresh(AppState *st) {
  int c = get_conservation(st);
  // Igual que cool_refresh: el cambio programático no debe re-disparar Set.
  st->cons_changing = true;
  if (st->cons_switch) {
    if (c >= 0) {
      gboolean want = c ? TRUE : FALSE;
      if (gtk_switch_get_active(st->cons_switch) != want)
        gtk_switch_set_active(st->cons_switch, want);
    }
    // conservation_mode es escribible vía sysfs con root; sin daemon solo lectura.
    gtk_widget_set_sensitive(GTK_WIDGET(st->cons_switch), st->daemon_available);
  }
  st->cons_changing = false;
  if (st->batt_label) {
    std::string cap = battery_capacity_sysfs();
    std::string status = battery_status_sysfs();
    std::string txt = "Batería: ";
    txt += cap.empty() ? "capacidad desconocida" : (cap + " %");
    if (!status.empty())
      txt += " (" + status + ")";
    if (c >= 0)
      txt += c ? " — conservation: activado (límite ~60 %)"
               : " — conservation: desactivado (carga completa)";
    gtk_label_set_text(st->batt_label, txt.c_str());
  }
  if (st->ppd_label) {
    std::string prof = get_ppd_profile();
    std::string txt = "power-profiles-daemon: ";
    txt += prof.empty() ? "no disponible" : ("perfil activo: " + prof);
    gtk_label_set_text(st->ppd_label, txt.c_str());
  }
}

static void on_cons_switch_notify(GObject *obj, GParamSpec *, gpointer data) {
  AppState *st = static_cast<AppState *>(data);
  if (st->cons_changing)
    return;
  if (!st->daemon_available) {
    energy_refresh(st);
    GtkWindow *win =
        GTK_WINDOW(gtk_application_get_active_window(GTK_APPLICATION(st->app)));
    show_error(win, "Daemon no disponible", kDaemonMissingMsg);
    return;
  }
  gboolean on = gtk_switch_get_active(GTK_SWITCH(obj));
  st->cons_changing = true;
  GError *err = nullptr;
  GVariant *r = proxy_call_set(st, "SetConservation",
                                g_variant_new("(b)", on ? TRUE : FALSE), &err);
  if (r)
    g_variant_unref(r);
  if (err) {
    GtkWindow *win =
        GTK_WINDOW(gtk_application_get_active_window(GTK_APPLICATION(st->app)));
    show_error(win, "No se pudo cambiar conservation_mode",
               err->message ? err->message : "Error D-Bus");
    g_error_free(err);
  }
  st->cons_changing = false;
  energy_refresh(st);
}

/* ------------------------------------------------------------------ */
/* Página Teclado                                                      */
/* ------------------------------------------------------------------ */

static void kbd_refresh(AppState *st) {
  int lvl = get_kbd_backlight(st);
  // El cambio programático no debe re-disparar SetKbdBacklight.
  st->kbd_changing = true;
  if (st->kbd_combo) {
    if (lvl >= 0 && lvl <= 2)
      adw_combo_row_set_selected(st->kbd_combo, static_cast<guint>(lvl));
    gtk_widget_set_sensitive(GTK_WIDGET(st->kbd_combo), st->daemon_available &&
                                                            kbd_brightness_path().empty() ==
                                                                false);
    if (kbd_brightness_path().empty())
      gtk_widget_set_sensitive(GTK_WIDGET(st->kbd_combo), FALSE);
    else if (!st->daemon_available)
      gtk_widget_set_sensitive(GTK_WIDGET(st->kbd_combo), FALSE);
  }
  st->kbd_changing = false;
}

static void on_kbd_combo_notify(GObject *obj, GParamSpec *, gpointer data) {
  AppState *st = static_cast<AppState *>(data);
  if (st->kbd_changing)
    return;
  guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(obj));
  if (sel > 2)
    return;
  if (!st->daemon_available) {
    GtkWindow *win =
        GTK_WINDOW(gtk_application_get_active_window(GTK_APPLICATION(st->app)));
    show_error(win, "Daemon no disponible", kDaemonMissingMsg);
    kbd_refresh(st);
    return;
  }
  st->kbd_changing = true;
  GError *err = nullptr;
  GVariant *r = proxy_call_set(st, "SetKbdBacklight", g_variant_new("(u)", sel), &err);
  if (r)
    g_variant_unref(r);
  if (err) {
    GtkWindow *win =
        GTK_WINDOW(gtk_application_get_active_window(GTK_APPLICATION(st->app)));
    show_error(win, "No se pudo cambiar el brillo del teclado",
               err->message ? err->message : "Error D-Bus");
    g_error_free(err);
  }
  st->kbd_changing = false;
  kbd_refresh(st);
}

/* ------------------------------------------------------------------ */
/* Página Sistema                                                      */
/* ------------------------------------------------------------------ */

/* Mapa clave→valor desde GetSystemInfo() del daemon, o vacío sin daemon. */
static std::map<std::string, std::string> daemon_system_info(AppState *st) {
  std::map<std::string, std::string> m;
  GError *err = nullptr;
  GVariant *r = proxy_call_sync(st, "GetSystemInfo", nullptr, &err);
  if (err)
    g_error_free(err);
  if (!r)
    return m;
  GVariantIter *iter = nullptr;
  const gchar *k = nullptr;
  const gchar *v = nullptr;
  g_variant_get(r, "(a{ss})", &iter);
  while (iter != nullptr && g_variant_iter_next(iter, "{&s&s}", &k, &v))
    m[k ? k : ""] = v ? v : "";
  if (iter != nullptr)
    g_variant_iter_free(iter);
  g_variant_unref(r);
  return m;
}

static std::string dmi_value(const std::map<std::string, std::string> &m,
                             const char *daemon_key, const char *dmi_field) {
  auto it = m.find(daemon_key);
  if (it != m.end() && !it->second.empty())
    return it->second;
  return read_dmi(dmi_field);
}

static void set_dmi_label(GtkLabel *label, const std::string &v) {
  if (label)
    gtk_label_set_text(label, v.empty() ? "—" : v.c_str());
}

static void system_refresh(AppState *st) {
  auto info = daemon_system_info(st);
  set_dmi_label(st->dmi_product_name, dmi_value(info, "product_name", "product_name"));
  set_dmi_label(st->dmi_product_version,
                dmi_value(info, "product_version", "product_version"));
  set_dmi_label(st->dmi_sys_vendor, dmi_value(info, "sys_vendor", "sys_vendor"));
  set_dmi_label(st->dmi_bios_version, dmi_value(info, "bios_version", "bios_version"));
  set_dmi_label(st->dmi_bios_date, dmi_value(info, "bios_date", "bios_date"));
  set_dmi_label(st->dmi_bios_vendor, dmi_value(info, "bios_vendor", "bios_vendor"));
  set_dmi_label(st->dmi_board_name, dmi_value(info, "board_name", "board_name"));
  set_dmi_label(st->dmi_board_version,
                dmi_value(info, "board_version", "board_version"));
  set_dmi_label(st->dmi_board_vendor, dmi_value(info, "board_vendor", "board_vendor"));
  // Las temperaturas se actualizan en sensors_tick (barras en vivo).
}

/* ------------------------------------------------------------------ */
/* Construcción de la ventana                                          */
/* ------------------------------------------------------------------ */

static AdwPreferencesGroup *new_group(const char *title) {
  AdwPreferencesGroup *g =
      ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(g, title);
  return g;
}

/* Fila "etiqueta … valor" como las ListBox del Glade original (System);
 * devuelve el label de valor para rellenarlo en system_refresh(). */
static GtkLabel *dmi_value_row(AdwPreferencesGroup *g, const char *label) {
  AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), label);
  GtkLabel *val = GTK_LABEL(gtk_label_new("—"));
  gtk_label_set_xalign(val, 1.0);
  gtk_label_set_selectable(val, TRUE);
  adw_action_row_add_suffix(row, GTK_WIDGET(val));
  adw_preferences_group_add(g, GTK_WIDGET(row));
  return val;
}

static GtkWidget *section_title(const char *markup_bold) {
  GtkWidget *l = gtk_label_new(nullptr);
  gtk_label_set_markup(GTK_LABEL(l), markup_bold);
  gtk_widget_set_halign(l, GTK_ALIGN_CENTER);
  return l;
}

/* Logo central de la página Fan (réplica del GtkImage main_logo.png):
 * prueba el SVG instalado y cae a icono simbólico del sistema. */
static GtkWidget *logo_widget() {
  for (int i = 0; kLogoPaths[i]; ++i) {
    std::ifstream f(kLogoPaths[i]);
    if (f.good()) {
      GtkWidget *pic = gtk_picture_new_for_filename(kLogoPaths[i]);
      gtk_picture_set_content_fit(GTK_PICTURE(pic), GTK_CONTENT_FIT_SCALE_DOWN);
      gtk_widget_set_size_request(pic, 128, 128);
      gtk_widget_set_halign(pic, GTK_ALIGN_CENTER);
      gtk_widget_set_margin_top(pic, 40);
      return pic;
    }
  }
  GtkWidget *img = gtk_image_new_from_icon_name(kIconFan);
  gtk_image_set_pixel_size(GTK_IMAGE(img), 96);
  gtk_widget_set_halign(img, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top(img, 40);
  return img;
}

static void on_about_clicked(GtkButton *, gpointer data) {
  AppState *st = static_cast<AppState *>(data);
  GtkWindow *parent =
      GTK_WINDOW(gtk_application_get_active_window(GTK_APPLICATION(st->app)));
  AdwDialog *dlg = ADW_DIALOG(adw_about_dialog_new());
  adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(dlg), kAboutProgram);
  adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(dlg), "org.lenovo.Y520Center");
  adw_about_dialog_set_version(ADW_ABOUT_DIALOG(dlg), kAboutVersion);
  adw_about_dialog_set_comments(ADW_ABOUT_DIALOG(dlg), kAboutComments);
  adw_about_dialog_set_website(ADW_ABOUT_DIALOG(dlg), kAboutWebsite);
  // Solo detalles + legal: sin créditos de desarrolladores.
  adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(dlg), GTK_LICENSE_GPL_3_0);
  adw_dialog_present(dlg, GTK_WIDGET(parent));
}

static void build_window(AppState *st) {
  // Réplica del Glade original (título + márgenes 18), adaptada a Wayland:
  // con HeaderBar (botones cerrar/min/max vía CSD), redimensionable y
  // páginas con scroll para que System no estire la ventana en altura.
  GtkWidget *win = adw_application_window_new(GTK_APPLICATION(st->app));
  gtk_window_set_title(GTK_WINDOW(win), "ExtremeCooling4Linux");
  gtk_window_set_default_size(GTK_WINDOW(win), 620, 800);
  gtk_window_set_resizable(GTK_WINDOW(win), TRUE);
  GtkWindow *win_ptr = GTK_WINDOW(win);

  GtkWidget *overlay = adw_toast_overlay_new();
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), overlay);

  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(overlay), vbox);

  GtkWidget *header = adw_header_bar_new();
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header),
                                  gtk_label_new("ExtremeCooling4Linux"));
  gtk_box_append(GTK_BOX(vbox), header);

  GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(body, 18);
  gtk_widget_set_margin_bottom(body, 18);
  gtk_widget_set_vexpand(body, TRUE);
  gtk_box_append(GTK_BOX(vbox), body);

  // Aviso fallback sin daemon (lectura sysfs directa).
  st->banner = ADW_BANNER(adw_banner_new(kDaemonMissingMsg));
  adw_banner_set_revealed(st->banner, st->daemon_available ? FALSE : TRUE);
  gtk_box_append(GTK_BOX(body), GTK_WIDGET(st->banner));

  // StackSwitcher centrado como id_main_stack_switcher (halign center, mx 12).
  GtkWidget *stack = adw_view_stack_new();
  // Sin homogeneidad: cada pestaña conserva su altura natural (System es
  // alta por sus 9 filas + sensores + núcleos); las cortas (Fan, Readme…)
  // ya no heredan ese alto como espacio vacío. System hace scroll.
  adw_view_stack_set_vhomogeneous(ADW_VIEW_STACK(stack), FALSE);
  adw_view_stack_set_hhomogeneous(ADW_VIEW_STACK(stack), FALSE);
  GtkWidget *switcher = adw_view_switcher_new();
  adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_STACK(stack));
  adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_SWITCHER_POLICY_WIDE);
  gtk_widget_set_halign(switcher, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start(switcher, 12);
  gtk_widget_set_margin_end(switcher, 12);
  gtk_box_append(GTK_BOX(body), switcher);
  gtk_box_append(GTK_BOX(body), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

  GtkWidget *pagebox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(pagebox, TRUE);
  gtk_widget_set_vexpand(pagebox, TRUE);
  gtk_widget_set_margin_start(pagebox, 18);
  gtk_widget_set_margin_end(pagebox, 18);
  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_widget_set_hexpand(scroll, TRUE);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), stack);
  gtk_box_append(GTK_BOX(pagebox), scroll);
  gtk_box_append(GTK_BOX(body), pagebox);

  /* ---- Página Fan Control (réplica id_fan_page del Glade) ---- */
  {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(page), section_title("<b>Extreme Cooling Configuration</b>"));
    GtkWidget *pad = gtk_label_new(nullptr);
    gtk_widget_set_margin_top(pad, 10);
    gtk_box_append(GTK_BOX(page), pad);
    gtk_box_append(GTK_BOX(page), logo_widget());

    // Formulario inferior centrado: "Enable Extreme Cooling" + Switch.
    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(form, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(form, 24);
    GtkWidget *lbl = gtk_label_new("Enable Extreme Cooling");
    gtk_widget_set_margin_start(lbl, 20);
    gtk_box_append(GTK_BOX(form), lbl);
    st->cool_switch = GTK_SWITCH(gtk_switch_new());
    gtk_widget_set_can_focus(GTK_WIDGET(st->cool_switch), TRUE);
    g_signal_connect(st->cool_switch, "notify::active",
                     G_CALLBACK(on_cool_switch_notify), st);
    gtk_box_append(GTK_BOX(form), GTK_WIDGET(st->cool_switch));
    gtk_box_append(GTK_BOX(page), form);

    AdwActionRow *srow = ADW_ACTION_ROW(adw_action_row_new());
    st->cool_state_label = GTK_LABEL(gtk_label_new("Estado: —"));
    gtk_label_set_xalign(st->cool_state_label, 0.0);
    gtk_label_set_wrap(st->cool_state_label, TRUE);
    adw_action_row_add_suffix(srow, GTK_WIDGET(st->cool_state_label));
    AdwPreferencesGroup *sg = new_group("Estado");
    adw_preferences_group_add(sg, GTK_WIDGET(srow));
    AdwActionRow *trow = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(trow), "Alternar estado");
    GtkWidget *btn = gtk_button_new_with_label("Toggle");
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_cool_toggle_clicked), st);
    adw_action_row_add_suffix(trow, btn);
    adw_preferences_group_add(sg, GTK_WIDGET(trow));
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(sg));

    AdwViewStackPage *sp = adw_view_stack_add_titled_with_icon(
        ADW_VIEW_STACK(stack), page, "fan", "Fan Control", kIconFan);
    (void)sp;
  }

  /* ---- Página Energía ---- */
  {
    GtkWidget *page = adw_preferences_page_new();
    adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(page),
                                       "battery-good-symbolic");
    AdwPreferencesGroup *g = new_group("Batería y energía");

    AdwActionRow *consrow = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(consrow), "Modo conservación");
    adw_action_row_set_subtitle(
        consrow, "Limita la carga (~60 %) vía ideapad_acpi conservation_mode.");
    st->cons_switch = GTK_SWITCH(gtk_switch_new());
    gtk_widget_set_valign(GTK_WIDGET(st->cons_switch), GTK_ALIGN_CENTER);
    g_signal_connect(st->cons_switch, "notify::active",
                     G_CALLBACK(on_cons_switch_notify), st);
    adw_action_row_add_suffix(consrow, GTK_WIDGET(st->cons_switch));
    adw_preferences_group_add(g, GTK_WIDGET(consrow));

    AdwActionRow *brow = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(brow), "Batería");
    st->batt_label = GTK_LABEL(gtk_label_new("Batería: —"));
    gtk_label_set_xalign(st->batt_label, 0.0);
    gtk_label_set_wrap(st->batt_label, TRUE);
    adw_action_row_add_suffix(brow, GTK_WIDGET(st->batt_label));
    adw_preferences_group_add(g, GTK_WIDGET(brow));

    AdwActionRow *prow = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(prow), "Perfil de energía");
    st->ppd_label = GTK_LABEL(gtk_label_new("power-profiles-daemon: —"));
    gtk_label_set_xalign(st->ppd_label, 0.0);
    gtk_label_set_wrap(st->ppd_label, TRUE);
    adw_action_row_add_suffix(prow, GTK_WIDGET(st->ppd_label));
    adw_preferences_group_add(g, GTK_WIDGET(prow));

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), g);
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(stack), page, "power",
                                          "Energía", kIconPower);
  }

  /* ---- Página Teclado ---- */
  {
    GtkWidget *page = adw_preferences_page_new();
    adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(page),
                                       "keyboard-symbolic");
    AdwPreferencesGroup *g = new_group("Retroiluminación");

    const char *levels[] = {kbd_level_label(0), kbd_level_label(1),
                              kbd_level_label(2), nullptr};
    GtkStringList *model = gtk_string_list_new(levels);
    st->kbd_combo = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(st->kbd_combo), "Brillo");
    adw_combo_row_set_model(st->kbd_combo, G_LIST_MODEL(model));
    g_object_unref(model);
    g_signal_connect(st->kbd_combo, "notify::selected",
                     G_CALLBACK(on_kbd_combo_notify), st);
    adw_preferences_group_add(g, GTK_WIDGET(st->kbd_combo));

    AdwActionRow *nrow = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nrow), "Nota");
    adw_action_row_set_subtitle(nrow, kKbdNote);
    adw_preferences_group_add(g, GTK_WIDGET(nrow));

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), g);
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(stack), page, "keyboard",
                                          "Teclado", kIconKbd);
  }

  /* ---- Página System (réplica id_system_page: Model/Bios/Board) ---- */
  {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(page), section_title("<b>System Information</b>"));
    GtkWidget *pad = gtk_label_new(nullptr);
    gtk_widget_set_margin_top(pad, 10);
    gtk_box_append(GTK_BOX(page), pad);

    AdwPreferencesGroup *gm = new_group("Model");
    st->dmi_product_name = dmi_value_row(gm, "Name");
    st->dmi_product_version = dmi_value_row(gm, "Version");
    st->dmi_sys_vendor = dmi_value_row(gm, "Vendor");
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(gm));

    AdwPreferencesGroup *gb = new_group("Bios");
    st->dmi_bios_version = dmi_value_row(gb, "Version");
    st->dmi_bios_date = dmi_value_row(gb, "Release Date");
    st->dmi_bios_vendor = dmi_value_row(gb, "Vendor");
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(gb));

    AdwPreferencesGroup *gbo = new_group("Board");
    st->dmi_board_name = dmi_value_row(gbo, "Name");
    st->dmi_board_version = dmi_value_row(gbo, "Version");
    st->dmi_board_vendor = dmi_value_row(gbo, "Vendor");
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(gbo));

    // Temperaturas en vivo: una fila por sensor con barra + °C.
    AdwPreferencesGroup *g2 = new_group("Temperaturas");
    {
      std::vector<TempSensor> sensors = discover_temp_sensors();
      if (sensors.empty()) {
        AdwActionRow *trow = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(trow),
                                      "Sensores hwmon no disponibles");
        adw_preferences_group_add(g2, GTK_WIDGET(trow));
      }
      for (const auto &s : sensors) {
        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), s.title.c_str());
        GtkLevelBar *bar = GTK_LEVEL_BAR(gtk_level_bar_new());
        gtk_level_bar_set_min_value(bar, 0.0);
        gtk_level_bar_set_max_value(bar, 1.0);
        gtk_level_bar_set_value(bar, 0.0);
        gtk_level_bar_set_mode(bar, GTK_LEVEL_BAR_MODE_CONTINUOUS);
        gtk_widget_set_hexpand(GTK_WIDGET(bar), TRUE);
        gtk_widget_set_valign(GTK_WIDGET(bar), GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(row, GTK_WIDGET(bar));
        GtkLabel *val = GTK_LABEL(gtk_label_new("—"));
        gtk_widget_set_size_request(GTK_WIDGET(val), 72, -1);
        gtk_label_set_xalign(val, 1.0);
        adw_action_row_add_suffix(row, GTK_WIDGET(val));
        adw_preferences_group_add(g2, GTK_WIDGET(row));
        st->temp_rows.push_back({bar, val, s});
      }
    }
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(g2));

    // Monitor de núcleos: fila por CPU con barra de nivel + % en vivo.
    AdwPreferencesGroup *gcpu = new_group("CPU (uso por núcleo)");
    {
      std::vector<unsigned long long> idle, total;
      cpu_stat_sample(idle, total);
      st->cpu_prev_idle = idle;
      st->cpu_prev_total = total;
      for (size_t i = 0; i < total.size(); ++i) {
        char name[32];
        snprintf(name, sizeof(name), "cpu%zu", i);
        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), name);
        GtkLevelBar *bar = GTK_LEVEL_BAR(gtk_level_bar_new());
        gtk_level_bar_set_min_value(bar, 0.0);
        gtk_level_bar_set_max_value(bar, 1.0);
        gtk_level_bar_set_value(bar, 0.0);
        gtk_level_bar_set_mode(bar, GTK_LEVEL_BAR_MODE_CONTINUOUS);
        gtk_widget_set_hexpand(GTK_WIDGET(bar), TRUE);
        gtk_widget_set_valign(GTK_WIDGET(bar), GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(row, GTK_WIDGET(bar));
        GtkLabel *pct = GTK_LABEL(gtk_label_new("0 %"));
        gtk_widget_set_size_request(GTK_WIDGET(pct), 52, -1);
        gtk_label_set_xalign(pct, 1.0);
        adw_action_row_add_suffix(row, GTK_WIDGET(pct));
        adw_preferences_group_add(gcpu, GTK_WIDGET(row));
        st->cpu_rows.push_back({bar, pct});
      }
    }
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(gcpu));

    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(stack), page, "system",
                                       "System", kIconComputer);
  }

  /* ---- Página Readme (réplica id_information_page del Glade) ---- */
  {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(page), section_title("<b>Important Information</b>"));
    GtkWidget *pad = gtk_label_new(nullptr);
    gtk_widget_set_margin_top(pad, 10);
    gtk_box_append(GTK_BOX(page), pad);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_justification(GTK_TEXT_VIEW(view), GTK_JUSTIFY_FILL);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buf, kReadmeText, -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_box_append(GTK_BOX(page), scroll);
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(stack), page, "readme",
                                       "Readme", kIconReadme);
  }

  gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

  // Botón About inferior centrado (réplica id_about_button del Glade).
  GtkWidget *aboutbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign(aboutbox, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start(aboutbox, 12);
  gtk_widget_set_margin_end(aboutbox, 12);
  GtkWidget *aboutbtn = gtk_button_new_with_label("About");
  g_signal_connect(aboutbtn, "clicked", G_CALLBACK(on_about_clicked), st);
  gtk_box_append(GTK_BOX(aboutbox), aboutbtn);
  gtk_box_append(GTK_BOX(vbox), aboutbox);

  (void)win_ptr;
  if (st->proxy)
    g_signal_connect(st->proxy, "g-signal", G_CALLBACK(on_daemon_signal), st);

  cool_refresh(st);
  energy_refresh(st);
  kbd_refresh(st);
  system_refresh(st);
  sensors_tick(st); // primera muestra inmediata
  g_timeout_add(2000, sensors_tick, st); // refresco de núcleos y temps cada 2 s

  gtk_window_present(win_ptr);
}

static void on_activate(AdwApplication *app, gpointer data) {
  AppState *st = static_cast<AppState *>(data);
  st->app = app;
  connect_daemon(st);
  build_window(st);
}

int main(int argc, char **argv) {
  AppState st;
  g_autoptr(AdwApplication) app =
      adw_application_new(kAppId, G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), &st);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  if (st.proxy)
    g_object_unref(st.proxy);
  return status;
}
