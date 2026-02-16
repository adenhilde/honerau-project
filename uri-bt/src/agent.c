#include "agent.h"
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void extract_mac(const char *device_path, char *mac_out);
static bool mac_allowed(const char *mac);
static void trust_device(const char *device_path);

static const char *whitelist[] = {
    "A4:F9:21:74:0B:C2",
    NULL
};

static DBusConnection *conn;

static bool mac_allowed(const char *mac)
{
    for (int i = 0; whitelist[i]; i++) {
        if (strcasecmp(mac, whitelist[i]) == 0)
            return true;
    }
    return false;
}

static void extract_mac(const char *device_path, char *mac_out)
{
    const char *p = strrchr(device_path, '/');
    if (!p) {
        mac_out[0] = '\0';
        return;
    }

    p++; // skip '/'
    if (strncmp(p, "dev_", 4) != 0) {
        mac_out[0] = '\0';
        return;
    }

    p += 4;

    // Convert AA_BB_CC_DD_EE_FF → AA:BB:CC:DD:EE:FF
    for (int i = 0; i < 17; i++) {
        mac_out[i] = (p[i] == '_') ? ':' : p[i];
    }
    mac_out[17] = '\0';
}

static void trust_device(const char *device_path)
{
    DBusMessage *msg;
    DBusMessageIter args, variant;
    dbus_bool_t val = 1;

    msg = dbus_message_new_method_call(
        "org.bluez",
        device_path,
        "org.freedesktop.DBus.Properties",
        "Set");

    const char *iface = "org.bluez.Device1";
    const char *prop  = "Trusted";

    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &prop);

    dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(&args, &variant);

    dbus_connection_send(conn, msg, NULL);
    dbus_message_unref(msg);
}

static DBusHandlerResult agent_message_handler(DBusConnection *connection,
                                               DBusMessage *message,
                                               void *user_data)
{
    if (dbus_message_is_method_call(message,
        "org.bluez.Agent1", "RequestConfirmation")) {

        const char *device;
        uint32_t passkey;
        DBusError err;
        char mac[18];   // ✅ DECLARED ONCE, IN SCOPE

        dbus_error_init(&err);

        if (!dbus_message_get_args(message, &err,
                                   DBUS_TYPE_OBJECT_PATH, &device,
                                   DBUS_TYPE_UINT32, &passkey,
                                   DBUS_TYPE_INVALID)) {
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        extract_mac(device, mac);
        printf("Pairing request from %s passkey=%06u\n", mac, passkey);

        if (!mac_allowed(mac)) {
            printf("Rejecting pairing from %s\n", mac);

            DBusMessage *err_reply =
                dbus_message_new_error(
                    message,
                    "org.bluez.Error.Rejected",
                    "Device not authorized");

            dbus_connection_send(connection, err_reply, NULL);
            dbus_message_unref(err_reply);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        printf("Accepting pairing from %s\n", mac);
        trust_device(device);

        DBusMessage *reply = dbus_message_new_method_return(message);
        dbus_connection_send(connection, reply, NULL);
        dbus_message_unref(reply);

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

bool agent_init(void)
{
    DBusError err;
    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!conn) {
        fprintf(stderr, "Failed to get system bus\n");
        return false;
    }

    dbus_connection_setup_with_g_main(conn, NULL);

    static DBusObjectPathVTable vtable = {
        .message_function = agent_message_handler
    };

    if (!dbus_connection_register_object_path(conn,
        "/uri/agent", &vtable, NULL)) {

        fprintf(stderr, "Failed to register agent object\n");
        return false;
    }

    DBusMessage *msg = dbus_message_new_method_call(
        "org.bluez",
        "/org/bluez",
        "org.bluez.AgentManager1",
        "RegisterAgent");

    const char *path = "/uri/agent";
    const char *capability = "KeyboardDisplay";

    dbus_message_append_args(msg,
        DBUS_TYPE_OBJECT_PATH, &path,
        DBUS_TYPE_STRING, &capability,
        DBUS_TYPE_INVALID);

    dbus_connection_send(conn, msg, NULL);
    dbus_message_unref(msg);

    msg = dbus_message_new_method_call(
        "org.bluez",
        "/org/bluez",
        "org.bluez.AgentManager1",
        "RequestDefaultAgent");

    dbus_message_append_args(msg,
        DBUS_TYPE_OBJECT_PATH, &path,
        DBUS_TYPE_INVALID);

    dbus_connection_send(conn, msg, NULL);
    dbus_message_unref(msg);

    printf("Agent registered\n");
    return true;
}

