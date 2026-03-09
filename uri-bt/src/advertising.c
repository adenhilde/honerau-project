// advertising.c
#include "advertising.h"
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <stdio.h>
#include <string.h>

/*
 * This must match your GATT service UUID in gatt.c
 */
#define SERVICE_UUID "12345678-1234-5678-1234-56789abcdef0"

/*
 * Object path for the advertisement
 */
#define ADV_PATH "/uri/advertisement0"

static DBusConnection *conn;

/*
 * Helper to append a {string, variant} dict entry of type "s"
 */
static void append_prop_string(DBusMessageIter *dict,
                               const char *key,
                               const char *value)
{
    DBusMessageIter entry, var;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
    dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

/*
 * Helper to append a {string, variant} dict entry of type "as" (array of strings)
 */
static void append_prop_string_array(DBusMessageIter *dict,
                                     const char *key,
                                     const char **values)
{
    DBusMessageIter entry, var, array;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &array);

    for (int i = 0; values[i]; i++) {
        const char *v = values[i];
        dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &v);
    }

    dbus_message_iter_close_container(&var, &array);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(dict, &entry);
}

/*
 * Handle DBus calls on ADV_PATH
 */
static DBusHandlerResult adv_handler(DBusConnection *c,
                                     DBusMessage *m,
                                     void *user_data)
{
    // BlueZ will call org.freedesktop.DBus.Properties.GetAll
    if (dbus_message_is_method_call(m,
        "org.freedesktop.DBus.Properties", "GetAll")) {

        const char *iface;
    DBusError err;
    dbus_error_init(&err);

    if (!dbus_message_get_args(m, &err,
        DBUS_TYPE_STRING, &iface,
        DBUS_TYPE_INVALID)) {
        fprintf(stderr, "adv_handler: GetAll missing iface arg\n");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        if (strcmp(iface, "org.bluez.LEAdvertisement1") != 0)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        DBusMessage *reply = dbus_message_new_method_return(m);
        DBusMessageIter iter, dict;

        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

        /* Type = "peripheral" */
        const char *type_val = "peripheral";
        append_prop_string(&dict, "Type", type_val);

        /* LocalName = "Hello Nick v2" */
        const char *name_val = "Hello Nick v2";
        append_prop_string(&dict, "LocalName", name_val);

        /* ServiceUUIDs = [ SERVICE_UUID ] */
        const char *uuids[] = { SERVICE_UUID, NULL };
        append_prop_string_array(&dict, "ServiceUUIDs", uuids);

        /* Includes = [ "tx-power" ] (optional) */
        const char *includes[] = { "tx-power", NULL };
        append_prop_string_array(&dict, "Includes", includes);

        dbus_message_iter_close_container(&iter, &dict);
        dbus_connection_send(c, reply, NULL);
        dbus_message_unref(reply);

        return DBUS_HANDLER_RESULT_HANDLED;
        }

        // BlueZ may call org.bluez.LEAdvertisement1.Release
        if (dbus_message_is_method_call(m,
            "org.bluez.LEAdvertisement1", "Release")) {

            printf("LEAdvertisement1.Release called by BlueZ\n");
        DBusMessage *reply = dbus_message_new_method_return(m);
        dbus_connection_send(c, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
            }

            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

bool advertising_init(void)
{
    DBusError err;
    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!conn) {
        fprintf(stderr, "advertising_init: failed to get system bus: %s\n",
                err.message ? err.message : "unknown error");
        return false;
    }

    dbus_connection_setup_with_g_main(conn, NULL);

    static DBusObjectPathVTable adv_vtable = {
        .message_function = adv_handler,
    };

    if (!dbus_connection_register_object_path(conn,
        ADV_PATH, &adv_vtable, NULL)) {

        fprintf(stderr, "advertising_init: register_object_path failed\n");
    return false;
        }

        // Register advertisement with LEAdvertisingManager1
        DBusMessage *msg = dbus_message_new_method_call(
            "org.bluez",
            "/org/bluez/hci0",
            "org.bluez.LEAdvertisingManager1",
            "RegisterAdvertisement");

        DBusMessageIter args, opts;
        dbus_message_iter_init_append(msg, &args);

        const char *path = ADV_PATH;
        dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &path);

        // options: empty dict
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &opts);
        dbus_message_iter_close_container(&args, &opts);

        dbus_connection_send(conn, msg, NULL);
        dbus_message_unref(msg);

        printf("LE Advertising registered (path=%s, uuid=%s)\n",
               ADV_PATH, SERVICE_UUID);

        return true;
}
