#include "gatt.h"
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <stdio.h>
#include <string.h>

static DBusConnection *conn;

/* UUIDs */
#define SERVICE_UUID "12345678-1234-5678-1234-56789abcdef0"
#define CHAR_UUID    "12345678-1234-5678-1234-56789abcdef1"

/* Object paths */
#define APP_PATH     "/uri/gatt"
#define SERVICE_PATH "/uri/gatt/service0"
#define CHAR_PATH    "/uri/gatt/service0/char0"

/* -------------------------------------------------- */
/* ObjectManager (application root)                   */
/* -------------------------------------------------- */

static DBusHandlerResult app_handler(DBusConnection *connection,
                                     DBusMessage *message,
                                     void *user_data)
{
    printf("GetManagedObjects called by BlueZ\n");
    if (!dbus_message_is_method_call(
            message,
            "org.freedesktop.DBus.ObjectManager",
            "GetManagedObjects"))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    DBusMessage *reply = dbus_message_new_method_return(message);
    DBusMessageIter iter, objects;

    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(
        &iter, DBUS_TYPE_ARRAY, "{oa{sa{sv}}}", &objects);

    DBusMessageIter obj, ifaces, iface, props, prop, var;

    /* ---------- Service ---------- */
    dbus_message_iter_open_container(&objects, DBUS_TYPE_DICT_ENTRY, NULL, &obj);
    const char *svc_path = SERVICE_PATH;
    dbus_message_iter_append_basic(&obj, DBUS_TYPE_OBJECT_PATH, &svc_path);

    dbus_message_iter_open_container(&obj, DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);
    dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &iface);

    const char *svc_iface = "org.bluez.GattService1";
    dbus_message_iter_append_basic(&iface, DBUS_TYPE_STRING, &svc_iface);
    dbus_message_iter_open_container(&iface, DBUS_TYPE_ARRAY, "{sv}", &props);

    /* UUID */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &prop);
    const char *key = "UUID";
    dbus_message_iter_append_basic(&prop, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&prop, DBUS_TYPE_VARIANT, "s", &var);
    const char *service_uuid = SERVICE_UUID;
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &service_uuid);
    dbus_message_iter_close_container(&prop, &var);
    dbus_message_iter_close_container(&props, &prop);

    /* Primary */
    dbus_bool_t primary = TRUE;
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &prop);
    const char *prim = "PRIMARY";
    dbus_message_iter_append_basic(&prop, DBUS_TYPE_STRING, &prim);
    dbus_message_iter_open_container(&prop, DBUS_TYPE_VARIANT, "b", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &primary);
    dbus_message_iter_close_container(&prop, &var);
    dbus_message_iter_close_container(&props, &prop);

    dbus_message_iter_close_container(&iface, &props);
    dbus_message_iter_close_container(&ifaces, &iface);
    dbus_message_iter_close_container(&obj, &ifaces);
    dbus_message_iter_close_container(&objects, &obj);

    /* ---------- Characteristic ---------- */
    dbus_message_iter_open_container(&objects, DBUS_TYPE_DICT_ENTRY, NULL, &obj);
    const char *char_path = CHAR_PATH;
    dbus_message_iter_append_basic(&obj, DBUS_TYPE_OBJECT_PATH, &char_path);

    dbus_message_iter_open_container(&obj, DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);
    dbus_message_iter_open_container(&ifaces, DBUS_TYPE_DICT_ENTRY, NULL, &iface);

    const char *char_iface = "org.bluez.GattCharacteristic1";
    dbus_message_iter_append_basic(&iface, DBUS_TYPE_STRING, &char_iface);
    dbus_message_iter_open_container(&iface, DBUS_TYPE_ARRAY, "{sv}", &props);

    /* UUID */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &prop);
    dbus_message_iter_append_basic(&prop, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&prop, DBUS_TYPE_VARIANT, "s", &var);
    const char *char_uuid = CHAR_UUID;
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &char_uuid);
    dbus_message_iter_close_container(&prop, &var);
    dbus_message_iter_close_container(&props, &prop);

    /* Service */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &prop);
    const char *ser = "Service";
    dbus_message_iter_append_basic(&prop, DBUS_TYPE_STRING, &ser);
    dbus_message_iter_open_container(&prop, DBUS_TYPE_VARIANT, "o", &var);
    const char *service_path = SERVICE_PATH;
    dbus_message_iter_append_basic(&var, DBUS_TYPE_OBJECT_PATH, &service_path);
    dbus_message_iter_close_container(&prop, &var);
    dbus_message_iter_close_container(&props, &prop);

    /* Flags */
    dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &prop);
    const char *fla = "Flags";
    dbus_message_iter_append_basic(&prop, DBUS_TYPE_STRING, &fla);
    dbus_message_iter_open_container(&prop, DBUS_TYPE_VARIANT, "as", &var);
    DBusMessageIter arr;
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &arr);
    const char *flag = "read";
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &flag);
    dbus_message_iter_close_container(&var, &arr);
    dbus_message_iter_close_container(&prop, &var);
    dbus_message_iter_close_container(&props, &prop);

    dbus_message_iter_close_container(&iface, &props);
    dbus_message_iter_close_container(&ifaces, &iface);
    dbus_message_iter_close_container(&obj, &ifaces);
    dbus_message_iter_close_container(&objects, &obj);

    dbus_message_iter_close_container(&iter, &objects);

    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

/* -------------------------------------------------- */
/* Characteristic methods                             */
/* -------------------------------------------------- */

static DBusHandlerResult char_handler(DBusConnection *connection,
                                      DBusMessage *message,
                                      void *user_data)
{
    if (dbus_message_is_method_call(
            message,
            "org.bluez.GattCharacteristic1",
            "ReadValue")) {

        DBusMessage *reply = dbus_message_new_method_return(message);
        DBusMessageIter iter, array;
        const char *value = "URI BLE OK";

        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "y", &array);

        for (size_t i = 0; i < strlen(value); i++)
            dbus_message_iter_append_basic(&array, DBUS_TYPE_BYTE, &value[i]);

        dbus_message_iter_close_container(&iter, &array);
        dbus_connection_send(connection, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* -------------------------------------------------- */
/* Init                                               */
/* -------------------------------------------------- */

bool gatt_init(void)
{
    DBusError err;
    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!conn) return false;

    dbus_connection_setup_with_g_main(conn, NULL);

    static DBusObjectPathVTable app_vtable = {
        .message_function = app_handler
    };
    static DBusObjectPathVTable char_vtable = {
        .message_function = char_handler
    };

    dbus_connection_register_object_path(conn, APP_PATH, &app_vtable, NULL);
    dbus_connection_register_object_path(conn, SERVICE_PATH, &app_vtable, NULL);
    dbus_connection_register_object_path(conn, CHAR_PATH, &char_vtable, NULL);

    DBusMessage *msg = dbus_message_new_method_call(
        "org.bluez",
        "/org/bluez/hci0",
        "org.bluez.GattManager1",
        "RegisterApplication");

    DBusMessageIter args, opts;
    dbus_message_iter_init_append(msg, &args);
    //dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &(const char *){APP_PATH});
    const char *app_path = APP_PATH;
    dbus_message_iter_append_basic(
        &args,
        DBUS_TYPE_OBJECT_PATH,
        &app_path
    );
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &opts);
    dbus_message_iter_close_container(&args, &opts);

    dbus_connection_send(conn, msg, NULL);
    dbus_message_unref(msg);

    printf("GATT registered\n");
    return true;
}

