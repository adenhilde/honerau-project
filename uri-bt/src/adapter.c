#include "adapter.h"
#include <dbus/dbus.h>
#include <stdio.h>

static DBusConnection *conn;

static void set_bool_property(const char *prop, dbus_bool_t value)
{
    DBusMessage *msg;
    DBusMessageIter args, variant;

    msg = dbus_message_new_method_call(
        "org.bluez",
        "/org/bluez/hci0",
        "org.freedesktop.DBus.Properties",
        "Set");

    const char *iface = "org.bluez.Adapter1";

    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &prop);

    dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(&args, &variant);

    dbus_connection_send(conn, msg, NULL);
    dbus_connection_flush(conn);
    dbus_message_unref(msg);
}

static void set_string_property(const char *prop, const char *value)
{
    DBusMessage *msg;
    DBusMessageIter args, variant;

    msg = dbus_message_new_method_call(
        "org.bluez",
        "/org/bluez/hci0",
        "org.freedesktop.DBus.Properties",
        "Set");

    const char *iface = "org.bluez.Adapter1";

    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &prop);

    dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&args, &variant);

    dbus_connection_send(conn, msg, NULL);
    dbus_connection_flush(conn);
    dbus_message_unref(msg);
}

bool adapter_init(void)
{
    DBusError err;
    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!conn) {
        fprintf(stderr, "Failed to connect system bus\n");
        return false;
    }

    set_bool_property("Powered", 1);             // 1
    set_bool_property("Discoverable", 1);        // 0
    set_bool_property("Pairable", 1);            // 0
    set_string_property("Privacy", "device");    // uncomment
    set_string_property("Alias", "Honeywell-URI-Device");


    printf("Adapter locked down\n");
    return true;
}

void adapter_set_pairable(bool enable)
{
    set_bool_property("Pairable", enable ? 1 : 0);
}

void adapter_set_discoverable(bool enable)
{
    set_bool_property("Discoverable", enable ? 1 : 0);
}

