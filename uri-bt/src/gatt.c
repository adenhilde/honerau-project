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
#define SERVICE_PATH "/uri/gatt/service0"
#define CHAR_PATH    "/uri/gatt/service0/char0"

static DBusHandlerResult app_handler(DBusConnection *connection,
                                     DBusMessage *message,
                                     void *user_data)
{
    if (dbus_message_is_method_call(message,
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects")) {

        DBusMessage *reply;
        DBusMessageIter iter, objects;

        reply = dbus_message_new_method_return(message);
        dbus_message_iter_init_append(reply, &iter);

        /* a{oa{sa{sv}}} */
        dbus_message_iter_open_container(&iter,
            DBUS_TYPE_ARRAY, "{oa{sa{sv}}}", &objects);

        /* ---- Service object ---- */
        DBusMessageIter obj_entry, ifaces, iface_entry, props, prop, variant;

        dbus_message_iter_open_container(&objects,
            DBUS_TYPE_DICT_ENTRY, NULL, &obj_entry);

        const char *svc_path = SERVICE_PATH;
        dbus_message_iter_append_basic(&obj_entry,
            DBUS_TYPE_OBJECT_PATH, &svc_path);

        dbus_message_iter_open_container(&obj_entry,
            DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);

        dbus_message_iter_open_container(&ifaces,
            DBUS_TYPE_DICT_ENTRY, NULL, &iface_entry);

        const char *svc_iface = "org.bluez.GattService1";
        dbus_message_iter_append_basic(&iface_entry,
            DBUS_TYPE_STRING, &svc_iface);

        dbus_message_iter_open_container(&iface_entry,
            DBUS_TYPE_ARRAY, "{sv}", &props);

        /* UUID */
        dbus_message_iter_open_container(&props,
            DBUS_TYPE_DICT_ENTRY, NULL, &prop);
        dbus_message_iter_append_basic(&prop,
            DBUS_TYPE_STRING, &(const char *){"UUID"});
        dbus_message_iter_open_container(&prop,
            DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant,
            DBUS_TYPE_STRING, &(const char *){SERVICE_UUID});
        dbus_message_iter_close_container(&prop, &variant);
        dbus_message_iter_close_container(&props, &prop);

        /* Primary */
        dbus_bool_t primary = 1;
        dbus_message_iter_open_container(&props,
            DBUS_TYPE_DICT_ENTRY, NULL, &prop);
        dbus_message_iter_append_basic(&prop,
            DBUS_TYPE_STRING, &(const char *){"Primary"});
        dbus_message_iter_open_container(&prop,
            DBUS_TYPE_VARIANT, "b", &variant);
        dbus_message_iter_append_basic(&variant,
            DBUS_TYPE_BOOLEAN, &primary);
        dbus_message_iter_close_container(&prop, &variant);
        dbus_message_iter_close_container(&props, &prop);

        dbus_message_iter_close_container(&iface_entry, &props);
        dbus_message_iter_close_container(&ifaces, &iface_entry);
        dbus_message_iter_close_container(&obj_entry, &ifaces);
        dbus_message_iter_close_container(&objects, &obj_entry);

        /* ---- Characteristic object ---- */
        dbus_message_iter_open_container(&objects,
            DBUS_TYPE_DICT_ENTRY, NULL, &obj_entry);

        const char *char_path = CHAR_PATH;
        dbus_message_iter_append_basic(&obj_entry,
            DBUS_TYPE_OBJECT_PATH, &char_path);

        dbus_message_iter_open_container(&obj_entry,
            DBUS_TYPE_ARRAY, "{sa{sv}}", &ifaces);

        dbus_message_iter_open_container(&ifaces,
            DBUS_TYPE_DICT_ENTRY, NULL, &iface_entry);

        const char *char_iface = "org.bluez.GattCharacteristic1";
        dbus_message_iter_append_basic(&iface_entry,
            DBUS_TYPE_STRING, &char_iface);

        dbus_message_iter_open_container(&iface_entry,
            DBUS_TYPE_ARRAY, "{sv}", &props);

        /* UUID */
        dbus_message_iter_open_container(&props,
            DBUS_TYPE_DICT_ENTRY, NULL, &prop);
        dbus_message_iter_append_basic(&prop,
            DBUS_TYPE_STRING, &(const char *){"UUID"});
        dbus_message_iter_open_container(&prop,
            DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant,
            DBUS_TYPE_STRING, &(const char *){CHAR_UUID});
        dbus_message_iter_close_container(&prop, &variant);
        dbus_message_iter_close_container(&props, &prop);

        /* Service */
        dbus_message_iter_open_container(&props,
            DBUS_TYPE_DICT_ENTRY, NULL, &prop);
        dbus_message_iter_append_basic(&prop,
            DBUS_TYPE_STRING, &(const char *){"Service"});
        dbus_message_iter_open_container(&prop,
            DBUS_TYPE_VARIANT, "o", &variant);
        dbus_message_iter_append_basic(&variant,
            DBUS_TYPE_OBJECT_PATH, &(const char *){SERVICE_PATH});
        dbus_message_iter_close_container(&prop, &variant);
        dbus_message_iter_close_container(&props, &prop);

        /* Flags */
        const char *flag = "read";
        dbus_message_iter_open_container(&props,
            DBUS_TYPE_DICT_ENTRY, NULL, &prop);
        dbus_message_iter_append_basic(&prop,
            DBUS_TYPE_STRING, &(const char *){"Flags"});
        dbus_message_iter_open_container(&prop,
            DBUS_TYPE_VARIANT, "as", &variant);
        DBusMessageIter arr;
        dbus_message_iter_open_container(&variant,
            DBUS_TYPE_ARRAY, "s", &arr);
        dbus_message_iter_append_basic(&arr,
            DBUS_TYPE_STRING, &flag);
        dbus_message_iter_close_container(&variant, &arr);
        dbus_message_iter_close_container(&prop, &variant);
        dbus_message_iter_close_container(&props, &prop);

        dbus_message_iter_close_container(&iface_entry, &props);
        dbus_message_iter_close_container(&ifaces, &iface_entry);
        dbus_message_iter_close_container(&obj_entry, &ifaces);
        dbus_message_iter_close_container(&objects, &obj_entry);

        dbus_message_iter_close_container(&iter, &objects);

        dbus_connection_send(connection, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


static DBusHandlerResult service_handler(DBusConnection *connection,
                                         DBusMessage *message,
                                         void *user_data)
{
    if (dbus_message_is_method_call(message,
        "org.freedesktop.DBus.Properties", "GetAll")) {

        const char *iface;
        DBusMessage *reply;
        DBusMessageIter iter, dict, entry, variant;

        dbus_message_get_args(message, NULL,
            DBUS_TYPE_STRING, &iface,
            DBUS_TYPE_INVALID);

        if (strcmp(iface, "org.bluez.GattService1") != 0)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        reply = dbus_message_new_method_return(message);
        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter,
            DBUS_TYPE_ARRAY, "{sv}", &dict);

        /* UUID */
        const char *uuid = SERVICE_UUID;
        dbus_message_iter_open_container(&dict,
            DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry,
            DBUS_TYPE_STRING, &(const char *){"UUID"});
        dbus_message_iter_open_container(&entry,
            DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant,
            DBUS_TYPE_STRING, &uuid);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&dict, &entry);

        /* Primary = true */
        dbus_bool_t primary = 1;
        dbus_message_iter_open_container(&dict,
            DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry,
            DBUS_TYPE_STRING, &(const char *){"Primary"});
        dbus_message_iter_open_container(&entry,
            DBUS_TYPE_VARIANT, "b", &variant);
        dbus_message_iter_append_basic(&variant,
            DBUS_TYPE_BOOLEAN, &primary);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&dict, &entry);

        dbus_message_iter_close_container(&iter, &dict);
        dbus_connection_send(connection, reply, NULL);
        dbus_message_unref(reply);

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* -------- Characteristic handler -------- */

static DBusHandlerResult char_handler(DBusConnection *connection,
                                      DBusMessage *message,
                                      void *user_data)
{
    /* Handle property queries */
    if (dbus_message_is_method_call(message,
        "org.freedesktop.DBus.Properties", "GetAll")) {

        const char *iface;
        DBusMessage *reply;
        DBusMessageIter iter, dict, entry, variant;

        dbus_message_get_args(message, NULL,
            DBUS_TYPE_STRING, &iface,
            DBUS_TYPE_INVALID);

        if (strcmp(iface, "org.bluez.GattCharacteristic1") != 0)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        reply = dbus_message_new_method_return(message);
        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter,
            DBUS_TYPE_ARRAY, "{sv}", &dict);

        /* UUID */
        const char *uuid = CHAR_UUID;
        dbus_message_iter_open_container(&dict,
            DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry,
            DBUS_TYPE_STRING, &(const char *){"UUID"});
        dbus_message_iter_open_container(&entry,
            DBUS_TYPE_VARIANT, "s", &variant);
        dbus_message_iter_append_basic(&variant,
            DBUS_TYPE_STRING, &uuid);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&dict, &entry);

        /* Service */
        const char *svc = SERVICE_PATH;
        dbus_message_iter_open_container(&dict,
            DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry,
            DBUS_TYPE_STRING, &(const char *){"Service"});
        dbus_message_iter_open_container(&entry,
            DBUS_TYPE_VARIANT, "o", &variant);
        dbus_message_iter_append_basic(&variant,
            DBUS_TYPE_OBJECT_PATH, &svc);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&dict, &entry);

        /* Flags = ["read"] */
        dbus_message_iter_open_container(&dict,
            DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry,
            DBUS_TYPE_STRING, &(const char *){"Flags"});
        dbus_message_iter_open_container(&entry,
            DBUS_TYPE_VARIANT, "as", &variant);

        DBusMessageIter array;
        const char *flag = "read";
        dbus_message_iter_open_container(&variant,
            DBUS_TYPE_ARRAY, "s", &array);
        dbus_message_iter_append_basic(&array,
            DBUS_TYPE_STRING, &flag);
        dbus_message_iter_close_container(&variant, &array);

        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&dict, &entry);

        dbus_message_iter_close_container(&iter, &dict);
        dbus_connection_send(connection, reply, NULL);
        dbus_message_unref(reply);

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* Handle ReadValue */
    if (dbus_message_is_method_call(message,
        "org.bluez.GattCharacteristic1", "ReadValue")) {

        DBusMessage *reply = dbus_message_new_method_return(message);
        DBusMessageIter iter, array;

        const char *value = "URI BLE OK";

        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter,
            DBUS_TYPE_ARRAY, "y", &array);

        for (size_t i = 0; i < strlen(value); i++)
            dbus_message_iter_append_basic(
                &array, DBUS_TYPE_BYTE, &value[i]);

        dbus_message_iter_close_container(&iter, &array);
        dbus_connection_send(connection, reply, NULL);
        dbus_message_unref(reply);

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* -------- GATT registration -------- */

bool gatt_init(void)
{
    DBusError err;
    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!conn) {
        fprintf(stderr, "GATT: failed to get system bus\n");
        return false;
    }

    dbus_connection_setup_with_g_main(conn, NULL);

    static DBusObjectPathVTable service_vtable = {
        .message_function = service_handler
    };

    dbus_connection_register_object_path(
        conn, SERVICE_PATH, &service_vtable, NULL);

    /* Register characteristic object */
    static DBusObjectPathVTable char_vtable = {
        .message_function = char_handler
    };

    if (!dbus_connection_register_object_path(
            conn, CHAR_PATH, &char_vtable, NULL)) {
        fprintf(stderr, "GATT: failed to register characteristic\n");
        return false;
    }

    static DBusObjectPathVTable app_vtable = {
        .message_function = char_handler
    };
    
    dbus_connection_register_object_path(
        conn, "/uri/gatt", &app_vtable, NULL);

    /* Register service + characteristic with BlueZ */
    DBusMessage *msg = dbus_message_new_method_call(
        "org.bluez",
        "/org/bluez/hci0",
        "org.bluez.GattManager1",
        "RegisterApplication");

    const char *app_path = "/uri/gatt";
    DBusMessageIter args, dict;

    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args,
        DBUS_TYPE_OBJECT_PATH, &app_path);

    dbus_message_iter_open_container(&args,
        DBUS_TYPE_ARRAY, "{sv}", &dict);
    dbus_message_iter_close_container(&args, &dict);

    dbus_connection_send(conn, msg, NULL);
    dbus_message_unref(msg);

    printf("GATT service registered\n");
    return true;
}

