// gatt.c
//
// Production-oriented BlueZ D-Bus GATT server with ENCRYPTION REQUIRED:
// - 1 custom service
// - 2 characteristics:
//     * CONTROL:  write + notify (+ encrypt-write + encrypt-notify)
//     * DATA:     write-without-response (+ encrypt-write)
//
// Implements:
// - org.freedesktop.DBus.ObjectManager.GetManagedObjects (app root)
// - org.freedesktop.DBus.Properties.GetAll (service + both characteristics)
// - org.bluez.GattCharacteristic1.WriteValue (CONTROL + DATA)
// - org.bluez.GattCharacteristic1.StartNotify/StopNotify (CONTROL)
// - Notifications via PropertiesChanged(Value) for CONTROL
//
// File transfer protocol supported:
//   START: 0x01 | fileSize(u32 LE) | filenameLen(u8) | filename(UTF-8 bytes)
//   CHUNK: 0x02 | seq(u16 LE) | data...
//   END:   0x03
//
// Server -> iOS notify (CONTROL Value):
//   0x10 | status(u8) | bytesReceived(u32 LE) | expectedSeq(u16 LE)
// status:
//   0 = OK, 1 = ERR, 2 = GAP, 3 = DONE
//
// Notes:
// - DATA writes are raw binary; do NOT treat as strings.
// - For best throughput, iOS should use .withoutResponse on DATA char and respect backpressure.
//
// Build deps: dbus-1, dbus-glib-lowlevel, glib
//

#include "gatt.h"
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/* ---------------- UUIDs (match iOS) ---------------- */

#define SERVICE_UUID    "12345678-1234-5678-1234-56789abcdef0"
#define CHAR_CTRL_UUID  "12345678-1234-5678-1234-56789abcdef1"
#define CHAR_DATA_UUID  "12345678-1234-5678-1234-56789abcdef2"

/* ---------------- Object paths ---------------- */

#define APP_PATH        "/uri/gatt"
#define SERVICE_PATH    "/uri/gatt/service0"
#define CHAR_CTRL_PATH  "/uri/gatt/service0/char0"
#define CHAR_DATA_PATH  "/uri/gatt/service0/char1"

/* ---------------- Globals ---------------- */

static DBusConnection *conn;

/* Notify state for CONTROL characteristic */
static bool ctrl_notifying = false;

/* CONTROL characteristic Value (what BlueZ exposes) */
static uint8_t ctrl_value[256];
static size_t  ctrl_value_len = 0;

/* ---------------- Utilities ---------------- */

static void hexdump_prefix(const char *tag, const uint8_t *p, size_t n, size_t max)
{
    size_t k = (n < max) ? n : max;
    fprintf(stdout, "%s len=%zu: ", tag, n);
    for (size_t i = 0; i < k; i++) fprintf(stdout, "%02X ", p[i]);
    if (k < n) fprintf(stdout, "...");
    fprintf(stdout, "\n");
}

static uint32_t rd_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0]
    | ((uint32_t)p[1] << 8)
    | ((uint32_t)p[2] << 16)
    | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool ensure_dir(const char *path)
{
    // mkdir -p equivalent (handles nested paths)
    char tmp[512];
    size_t len = strnlen(path, sizeof(tmp) - 1);
    if (len == 0 || len >= sizeof(tmp) - 1) return false;
    memcpy(tmp, path, len);
    tmp[len] = '\0';

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                fprintf(stderr, "mkdir('%s') failed: %s\n", tmp, strerror(errno));
                return false;
            }
            tmp[i] = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir('%s') failed: %s\n", tmp, strerror(errno));
        return false;
    }
    return true;
}

static void sanitize_filename(char *s)
{
    // Replace '/' '\' and control chars
    for (size_t i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c == 127 || c == '/' || c == '\\') s[i] = '_';
    }
}

/* ---------------- CONTROL notify helper ---------------- */

static void emit_ctrl_value_changed(void)
{
    if (!ctrl_notifying) return;

    DBusMessage *sig = dbus_message_new_signal(
        CHAR_CTRL_PATH,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged"
    );
    if (!sig) return;

    const char *iface = "org.bluez.GattCharacteristic1";

    DBusMessageIter it, changed, entry, var, arr, inval;
    dbus_message_iter_init_append(sig, &it);

    // arg0: interface name
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);

    // arg1: dict<string, variant> changed props
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &changed);

    // entry: "Value" => variant(ay)
    const char *key = "Value";
    dbus_message_iter_open_container(&changed, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "y", &arr);

    for (size_t i = 0; i < ctrl_value_len; i++) {
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &ctrl_value[i]);
    }

    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&changed, &entry);

    dbus_message_iter_close_container(&it, &changed);

    // arg2: array<string> invalidated props
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &inval);
    dbus_message_iter_close_container(&it, &inval);

    dbus_connection_send(conn, sig, NULL);
    dbus_message_unref(sig);
}

/*
 * Server -> iOS status packet format (CONTROL notify):
 *   0x10 | status(u8) | bytesReceived(u32 LE) | expectedSeq(u16 LE)
 */
static void ctrl_notify_status(uint8_t status, uint32_t bytes, uint16_t expected_seq)
{
    ctrl_value_len = 0;
    ctrl_value[ctrl_value_len++] = 0x10;
    ctrl_value[ctrl_value_len++] = status;

    ctrl_value[ctrl_value_len++] = (uint8_t)(bytes & 0xFF);
    ctrl_value[ctrl_value_len++] = (uint8_t)((bytes >> 8) & 0xFF);
    ctrl_value[ctrl_value_len++] = (uint8_t)((bytes >> 16) & 0xFF);
    ctrl_value[ctrl_value_len++] = (uint8_t)((bytes >> 24) & 0xFF);

    ctrl_value[ctrl_value_len++] = (uint8_t)(expected_seq & 0xFF);
    ctrl_value[ctrl_value_len++] = (uint8_t)((expected_seq >> 8) & 0xFF);

    emit_ctrl_value_changed();
}

/* ---------------- File transfer state ---------------- */

typedef struct {
    bool active;
    uint32_t file_size;
    uint32_t bytes_received;
    uint16_t expected_seq;

    int fd;
    char final_path[512];
    char temp_path[512];

    time_t start_time;
} transfer_t;

static transfer_t T = {0};

static const char *pick_base_dir(void)
{
    // Prefer ~/Desktop/uri_recv ; fallback ~/uri_recv ; fallback /tmp/uri_recv
    static char base[512];

    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(base, sizeof(base), "%s/Desktop/uri_recv", home);
        if (ensure_dir(base)) return base;

        snprintf(base, sizeof(base), "%s/uri_recv", home);
        if (ensure_dir(base)) return base;
    }

    snprintf(base, sizeof(base), "%s", "/tmp/uri_recv");
    if (ensure_dir(base)) return base;

    return "/tmp";
}

static void transfer_reset(void)
{
    if (T.fd >= 0) close(T.fd);
    T = (transfer_t){0};
    T.fd = -1;
}

static bool transfer_start(uint32_t file_size, const uint8_t *name, uint8_t name_len)
{
    transfer_reset();

    char filename[256];
    size_t n = (name_len < sizeof(filename) - 1) ? name_len : (sizeof(filename) - 1);
    memcpy(filename, name, n);
    filename[n] = '\0';
    sanitize_filename(filename);

    const char *base = pick_base_dir();

    snprintf(T.final_path, sizeof(T.final_path), "%s/%s", base, filename);

    char stamp[32];
    snprintf(stamp, sizeof(stamp), "%ld", (long)time(NULL));
    snprintf(T.temp_path, sizeof(T.temp_path), "%s/.%s.%s.part", base, filename, stamp);

    T.fd = open(T.temp_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (T.fd < 0) {
        fprintf(stderr, "open('%s') failed: %s\n", T.temp_path, strerror(errno));
        transfer_reset();
        return false;
    }

    T.active = true;
    T.file_size = file_size;
    T.bytes_received = 0;
    T.expected_seq = 0;
    T.start_time = time(NULL);

    fprintf(stdout, "START: file_size=%u name='%s'\n", T.file_size, filename);
    fprintf(stdout, "  temp='%s'\n  final='%s'\n", T.temp_path, T.final_path);

    ctrl_notify_status(0, T.bytes_received, T.expected_seq);
    return true;
}

static bool transfer_write_chunk(uint16_t seq, const uint8_t *data, size_t data_len)
{
    if (!T.active || T.fd < 0) return false;

    if (seq != T.expected_seq) {
        fprintf(stderr, "CHUNK GAP: got seq=%u expected=%u (len=%zu)\n",
                seq, T.expected_seq, data_len);

        ctrl_notify_status(2, T.bytes_received, T.expected_seq);

        // For now: reject out-of-order; client can retry from expected_seq
        return false;
    }

    ssize_t w = write(T.fd, data, data_len);
    if (w < 0) {
        fprintf(stderr, "write() failed: %s\n", strerror(errno));
        ctrl_notify_status(1, T.bytes_received, T.expected_seq);
        return false;
    }

    T.bytes_received += (uint32_t)w;
    T.expected_seq++;

    // progress notify every ~32KB
    if ((T.bytes_received & 0x7FFF) < (uint32_t)w) {
        ctrl_notify_status(0, T.bytes_received, T.expected_seq);
    }

    return true;
}

static bool transfer_end(void)
{
    if (!T.active || T.fd < 0) return false;

    if (fsync(T.fd) != 0) {
        fprintf(stderr, "fsync() failed: %s\n", strerror(errno));
        ctrl_notify_status(1, T.bytes_received, T.expected_seq);
        transfer_reset();
        return false;
    }

    close(T.fd);
    T.fd = -1;

    if (rename(T.temp_path, T.final_path) != 0) {
        fprintf(stderr, "rename('%s'->'%s') failed: %s\n",
                T.temp_path, T.final_path, strerror(errno));
        ctrl_notify_status(1, T.bytes_received, T.expected_seq);
        transfer_reset();
        return false;
    }

    time_t now = time(NULL);
    double dt = difftime(now, T.start_time);
    if (dt < 0.001) dt = 0.001;

    double rate = (double)T.bytes_received / dt;

    fprintf(stdout, "END: bytes=%u (declared=%u) duration=%.2fs rate=%.1f B/s\n",
            T.bytes_received, T.file_size, dt, rate);
    fprintf(stdout, "Saved: %s\n", T.final_path);

    ctrl_notify_status(3, T.bytes_received, T.expected_seq);
    transfer_reset();
    return true;
}

/* ---------------- DBus property helpers ---------------- */

static void append_prop_string(DBusMessageIter *props, const char *key, const char *val)
{
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(props, &entry);
}

static void append_prop_bool(DBusMessageIter *props, const char *key, dbus_bool_t b)
{
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &b);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(props, &entry);
}

static void append_prop_objpath(DBusMessageIter *props, const char *key, const char *path)
{
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_OBJECT_PATH, &path);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(props, &entry);
}

static void append_prop_str_array(DBusMessageIter *props, const char *key, const char **vals)
{
    DBusMessageIter entry, var, arr;
    dbus_message_iter_open_container(props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &arr);

    for (int i = 0; vals[i]; i++) {
        const char *v = vals[i];
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &v);
    }

    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(props, &entry);
}

static void append_prop_byte_array(DBusMessageIter *props, const char *key, const uint8_t *bytes, size_t n)
{
    DBusMessageIter entry, var, arr;
    dbus_message_iter_open_container(props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "y", &arr);

    for (size_t i = 0; i < n; i++) {
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_BYTE, &bytes[i]);
    }

    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(props, &entry);
}

/* ---------------- ObjectManager: GetManagedObjects ---------------- */

static DBusHandlerResult app_handler(DBusConnection *c, DBusMessage *m, void *u)
{
    (void)u;

    if (!dbus_message_is_method_call(m,
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects"))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    fprintf(stdout, "GetManagedObjects called by BlueZ\n");

    DBusMessage *reply = dbus_message_new_method_return(m);
    DBusMessageIter iter, objects;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{oa{sa{sv}}}", &objects);

    /* ----- Service object ----- */
    {
        DBusMessageIter obj, ifaces, iface, props;

        const char *path = SERVICE_PATH;
        const char *iface_name = "org.bluez.GattService1";

        dbus_message_iter_open_container(&objects, DBUS_TYPE_DICT_ENTRY, NULL, &obj);
        dbus_message_iter_append_basic(&obj, DBUS_TYPE_OBJECT_PATH, &path);
        dbus_message_iter_open_container(&obj, DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);

        dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &iface);
        dbus_message_iter_append_basic(&iface, DBUS_TYPE_STRING, &iface_name);
        dbus_message_iter_open_container(&iface, DBUS_TYPE_ARRAY, "{sv}", &props);

        append_prop_string(&props, "UUID", SERVICE_UUID);
        append_prop_bool(&props, "Primary", TRUE);

        dbus_message_iter_close_container(&iface, &props);
        dbus_message_iter_close_container(&ifaces, &iface);
        dbus_message_iter_close_container(&obj, &ifaces);
        dbus_message_iter_close_container(&objects, &obj);
    }

    /* ----- CONTROL characteristic ----- */
    {
        DBusMessageIter obj, ifaces, iface, props;

        const char *path = CHAR_CTRL_PATH;
        const char *iface_name = "org.bluez.GattCharacteristic1";
        const char *svc_path = SERVICE_PATH;

        dbus_message_iter_open_container(&objects, DBUS_TYPE_DICT_ENTRY, NULL, &obj);
        dbus_message_iter_append_basic(&obj, DBUS_TYPE_OBJECT_PATH, &path);
        dbus_message_iter_open_container(&obj, DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);

        dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &iface);
        dbus_message_iter_append_basic(&iface, DBUS_TYPE_STRING, &iface_name);
        dbus_message_iter_open_container(&iface, DBUS_TYPE_ARRAY, "{sv}", &props);

        append_prop_string(&props, "UUID", CHAR_CTRL_UUID);
        append_prop_objpath(&props, "Service", svc_path);

        // ENCRYPTION REQUIRED for write+notify
        const char *flags[] = { "write", "notify", "encrypt-write", "encrypt-notify", NULL };
        append_prop_str_array(&props, "Flags", flags);

        // Value property (used for notify)
        append_prop_byte_array(&props, "Value", ctrl_value, ctrl_value_len);

        dbus_message_iter_close_container(&iface, &props);
        dbus_message_iter_close_container(&ifaces, &iface);
        dbus_message_iter_close_container(&obj, &ifaces);
        dbus_message_iter_close_container(&objects, &obj);
    }

    /* ----- DATA characteristic ----- */
    {
        DBusMessageIter obj, ifaces, iface, props;

        const char *path = CHAR_DATA_PATH;
        const char *iface_name = "org.bluez.GattCharacteristic1";
        const char *svc_path = SERVICE_PATH;

        dbus_message_iter_open_container(&objects, DBUS_TYPE_DICT_ENTRY, NULL, &obj);
        dbus_message_iter_append_basic(&obj, DBUS_TYPE_OBJECT_PATH, &path);
        dbus_message_iter_open_container(&obj, DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);

        dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &iface);
        dbus_message_iter_append_basic(&iface, DBUS_TYPE_STRING, &iface_name);
        dbus_message_iter_open_container(&iface, DBUS_TYPE_ARRAY, "{sv}", &props);

        append_prop_string(&props, "UUID", CHAR_DATA_UUID);
        append_prop_objpath(&props, "Service", svc_path);

        // ENCRYPTION REQUIRED for streaming writes
        const char *flags[] = { "write-without-response", "encrypt-write", NULL };
        append_prop_str_array(&props, "Flags", flags);

        dbus_message_iter_close_container(&iface, &props);
        dbus_message_iter_close_container(&ifaces, &iface);
        dbus_message_iter_close_container(&obj, &ifaces);
        dbus_message_iter_close_container(&objects, &obj);
    }

    dbus_message_iter_close_container(&iter, &objects);
    dbus_connection_send(c, reply, NULL);
    dbus_message_unref(reply);

    return DBUS_HANDLER_RESULT_HANDLED;
}

/* ---------------- Properties.GetAll for Service / Chars ---------------- */

static DBusHandlerResult props_getall_service(DBusConnection *c, DBusMessage *m)
{
    const char *iface = NULL;
    DBusError err;
    dbus_error_init(&err);

    if (!dbus_message_get_args(m, &err,
        DBUS_TYPE_STRING, &iface,
        DBUS_TYPE_INVALID)) {
        fprintf(stderr, "GetAll(service) missing iface: %s\n", err.message ? err.message : "err");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
        if (strcmp(iface, "org.bluez.GattService1") != 0)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    DBusMessage *reply = dbus_message_new_method_return(m);
    DBusMessageIter it, props;
    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &props);

    append_prop_string(&props, "UUID", SERVICE_UUID);
    append_prop_bool(&props, "Primary", TRUE);

    dbus_message_iter_close_container(&it, &props);
    dbus_connection_send(c, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static void fill_char_props(DBusMessageIter *props,
                            const char *uuid,
                            const char *svc_path,
                            const char **flags,
                            bool include_value)
{
    append_prop_string(props, "UUID", uuid);
    append_prop_objpath(props, "Service", svc_path);
    append_prop_str_array(props, "Flags", flags);
    if (include_value) append_prop_byte_array(props, "Value", ctrl_value, ctrl_value_len);
}

static DBusHandlerResult props_getall_char(DBusConnection *c, DBusMessage *m, bool is_ctrl)
{
    const char *iface = NULL;
    DBusError err;
    dbus_error_init(&err);

    if (!dbus_message_get_args(m, &err,
        DBUS_TYPE_STRING, &iface,
        DBUS_TYPE_INVALID)) {
        fprintf(stderr, "GetAll(char) missing iface: %s\n", err.message ? err.message : "err");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
        if (strcmp(iface, "org.bluez.GattCharacteristic1") != 0)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    DBusMessage *reply = dbus_message_new_method_return(m);
    DBusMessageIter it, props;
    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &props);

    const char *svc_path = SERVICE_PATH;

    if (is_ctrl) {
        const char *flags[] = { "write", "notify", "encrypt-write", "encrypt-notify", NULL };
        fill_char_props(&props, CHAR_CTRL_UUID, svc_path, flags, true);
    } else {
        const char *flags[] = { "write-without-response", "encrypt-write", NULL };
        fill_char_props(&props, CHAR_DATA_UUID, svc_path, flags, false);
    }

    dbus_message_iter_close_container(&it, &props);
    dbus_connection_send(c, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

/* ---------------- GattCharacteristic1 handlers ---------------- */

static bool extract_ay_from_writevalue(DBusMessage *m, const uint8_t **out, int *out_len)
{
    DBusMessageIter args;
    if (!dbus_message_iter_init(m, &args)) return false;

    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY)
        return false;

    DBusMessageIter array;
    dbus_message_iter_recurse(&args, &array);

    uint8_t *data = NULL;
    int len = 0;

    dbus_message_iter_get_fixed_array(&array, &data, &len);

    if (!data || len <= 0) {
        *out = NULL;
        *out_len = 0;
        return true; // valid but empty
    }

    *out = data;
    *out_len = len;
    return true;
}

static void reply_ok(DBusConnection *c, DBusMessage *m)
{
    DBusMessage *reply = dbus_message_new_method_return(m);
    dbus_connection_send(c, reply, NULL);
    dbus_message_unref(reply);
}

static void reply_err(DBusConnection *c, DBusMessage *m, const char *name, const char *msg)
{
    DBusMessage *reply = dbus_message_new_error(m, name, msg);
    dbus_connection_send(c, reply, NULL);
    dbus_message_unref(reply);
}

/* CONTROL characteristic: GetAll / StartNotify / StopNotify / WriteValue */
static DBusHandlerResult char_ctrl_handler(DBusConnection *c, DBusMessage *m, void *u)
{
    (void)u;

    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "GetAll"))
        return props_getall_char(c, m, true);

    if (dbus_message_is_method_call(m, "org.bluez.GattCharacteristic1", "StartNotify")) {
        ctrl_notifying = true;
        fprintf(stdout, "CONTROL: StartNotify\n");
        emit_ctrl_value_changed();
        reply_ok(c, m);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(m, "org.bluez.GattCharacteristic1", "StopNotify")) {
        ctrl_notifying = false;
        fprintf(stdout, "CONTROL: StopNotify\n");
        reply_ok(c, m);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(m, "org.bluez.GattCharacteristic1", "WriteValue")) {
        const uint8_t *p = NULL;
        int n = 0;

        if (!extract_ay_from_writevalue(m, &p, &n)) {
            reply_err(c, m, "org.bluez.Error.InvalidValueLength", "WriteValue missing ay");
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        if (n <= 0) {
            fprintf(stdout, "CONTROL WriteValue: empty\n");
            reply_ok(c, m);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        hexdump_prefix("CONTROL WriteValue", p, (size_t)n, 32);

        uint8_t type = p[0];

        if (type == 0x01) {
            // START: 01 | fileSize(4) | nameLen(1) | name...
            if (n < 1 + 4 + 1) {
                fprintf(stderr, "START too short\n");
                ctrl_notify_status(1, T.bytes_received, T.expected_seq);
                reply_ok(c, m);
                return DBUS_HANDLER_RESULT_HANDLED;
            }

            uint32_t file_size = rd_u32_le(p + 1);
            uint8_t name_len = p[1 + 4];

            if ((int)(1 + 4 + 1 + name_len) > n) {
                fprintf(stderr, "START name_len exceeds packet\n");
                ctrl_notify_status(1, T.bytes_received, T.expected_seq);
                reply_ok(c, m);
                return DBUS_HANDLER_RESULT_HANDLED;
            }

            const uint8_t *name = p + 1 + 4 + 1;

            if (!transfer_start(file_size, name, name_len)) {
                ctrl_notify_status(1, 0, 0);
            }

            reply_ok(c, m);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        if (type == 0x03) {
            fprintf(stdout, "END received\n");
            (void)transfer_end();
            reply_ok(c, m);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        if (type == 0xFF) {
            fprintf(stdout, "ABORT received\n");
            transfer_reset();
            ctrl_notify_status(1, 0, 0);
            reply_ok(c, m);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        fprintf(stdout, "CONTROL: unknown type=0x%02X\n", type);
        reply_ok(c, m);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* DATA characteristic: GetAll / WriteValue */
static DBusHandlerResult char_data_handler(DBusConnection *c, DBusMessage *m, void *u)
{
    (void)u;

    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "GetAll"))
        return props_getall_char(c, m, false);

    if (dbus_message_is_method_call(m, "org.bluez.GattCharacteristic1", "WriteValue")) {
        const uint8_t *p = NULL;
        int n = 0;

        if (!extract_ay_from_writevalue(m, &p, &n)) {
            reply_err(c, m, "org.bluez.Error.InvalidValueLength", "WriteValue missing ay");
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        if (n <= 0) {
            reply_ok(c, m);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        uint8_t type = p[0];
        if (type != 0x02) {
            hexdump_prefix("DATA non-chunk", p, (size_t)n, 16);
            reply_ok(c, m);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        if (n < 1 + 2) {
            fprintf(stderr, "CHUNK too short\n");
            ctrl_notify_status(1, T.bytes_received, T.expected_seq);
            reply_ok(c, m);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        uint16_t seq = rd_u16_le(p + 1);
        const uint8_t *payload = p + 1 + 2;
        size_t payload_len = (size_t)n - (1 + 2);

        if ((seq % 64) == 0) {
            fprintf(stdout, "CHUNK seq=%u payload=%zu\n", seq, payload_len);
        }

        (void)transfer_write_chunk(seq, payload, payload_len);

        reply_ok(c, m);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* SERVICE object: Properties.GetAll */
static DBusHandlerResult service_handler(DBusConnection *c, DBusMessage *m, void *u)
{
    (void)u;

    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "GetAll"))
        return props_getall_service(c, m);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------------- Init ---------------- */

bool gatt_init(void)
{
    DBusError err;
    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!conn) {
        fprintf(stderr, "gatt_init: failed to get system bus: %s\n",
                err.message ? err.message : "unknown");
        return false;
    }

    dbus_connection_setup_with_g_main(conn, NULL);

    static DBusObjectPathVTable app_vtable  = { .message_function = app_handler };
    static DBusObjectPathVTable svc_vtable  = { .message_function = service_handler };
    static DBusObjectPathVTable ctrl_vtable = { .message_function = char_ctrl_handler };
    static DBusObjectPathVTable data_vtable = { .message_function = char_data_handler };

    if (!dbus_connection_register_object_path(conn, APP_PATH, &app_vtable, NULL)) {
        fprintf(stderr, "gatt_init: failed to register APP_PATH\n");
        return false;
    }

    if (!dbus_connection_register_object_path(conn, SERVICE_PATH, &svc_vtable, NULL)) {
        fprintf(stderr, "gatt_init: failed to register SERVICE_PATH\n");
        return false;
    }

    if (!dbus_connection_register_object_path(conn, CHAR_CTRL_PATH, &ctrl_vtable, NULL)) {
        fprintf(stderr, "gatt_init: failed to register CHAR_CTRL_PATH\n");
        return false;
    }

    if (!dbus_connection_register_object_path(conn, CHAR_DATA_PATH, &data_vtable, NULL)) {
        fprintf(stderr, "gatt_init: failed to register CHAR_DATA_PATH\n");
        return false;
    }

    DBusMessage *msg = dbus_message_new_method_call(
        "org.bluez", "/org/bluez/hci0",
        "org.bluez.GattManager1", "RegisterApplication"
    );

    DBusMessageIter args, opts;
    dbus_message_iter_init_append(msg, &args);

    const char *app_path = APP_PATH;
    dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &app_path);

    // options = empty dict
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &opts);
    dbus_message_iter_close_container(&args, &opts);

    dbus_connection_send(conn, msg, NULL);
    dbus_message_unref(msg);

    transfer_reset();
    fprintf(stdout, "GATT registered (ENCRYPTION REQUIRED):\n");
    fprintf(stdout, "  Service: %s\n", SERVICE_UUID);
    fprintf(stdout, "  CTRL   : %s (write+notify+encrypt) path=%s\n", CHAR_CTRL_UUID, CHAR_CTRL_PATH);
    fprintf(stdout, "  DATA   : %s (WNR+encrypt)         path=%s\n", CHAR_DATA_UUID, CHAR_DATA_PATH);

    return true;
}
