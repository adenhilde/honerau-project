#include <glib.h>
#include <unistd.h>
#include <dbus/dbus.h>
#include <signal.h>
#include "adapter.h"
#include "agent.h"
#include "gatt.h"

static GMainLoop *loop;

static void sig_handler(int sig)
{
    g_main_loop_quit(loop);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    loop = g_main_loop_new(NULL, FALSE);

    if (!adapter_init()) {
        g_printerr("Adapter init failed\n");
        return 1;
    }

    if (!agent_init()) {
        g_printerr("Agent init failed\n");
        return 1;
    }

    if (!gatt_init()) {
        g_printerr("GATT init failed\n");
        return 1;
    }

    g_print("uri-bt started\n");

    g_main_loop_run(loop);

    g_print("uri-bt exiting\n");
    return 0;
}

