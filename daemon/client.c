/*
 * Copyright (c) 2021 Open Mobile Platform LLC.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "config.h"
#include "logging.h"
#include "util.h"
#include "service.h"
#include "stringset.h"

#include <getopt.h>

#include <gio/gio.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * UTILITY
 * ------------------------------------------------------------------------- */

static void   client_usage      (const char *progname);
gchar       **prompt_permissions(GDBusConnection *connection, const char *application);
GHashTable   *query_appinfo     (GDBusConnection *connection, const char *application);
static bool   validate_args     (int argc, char **argv, const char *exec);
static int    client_exec       (const char *desktop, int argc, char **argv);
int           client_main       (int argc, char **argv);

/* ------------------------------------------------------------------------- *
 * APPINFO_PROPERTY
 * ------------------------------------------------------------------------- */

const char  *appinfo_get_string(GHashTable *appinfo, const char *key);
const char **appinfo_get_strv  (GHashTable *appinfo, const char *key);

/* ------------------------------------------------------------------------- *
 * APPINFO
 * ------------------------------------------------------------------------- */

const char *appinfo_deskop_exec               (GHashTable *appinfo);
const char *appinfo_sailjail_organization_name(GHashTable *appinfo);
const char *appinfo_sailjail_application_name (GHashTable *appinfo);
const char *appinfo_maemo_service             (GHashTable *appinfo);
const char *appinfo_maemo_method              (GHashTable *appinfo);

/* ------------------------------------------------------------------------- *
 * APPINFO_PERMISSIONS
 * ------------------------------------------------------------------------- */

const char **appinfo_sailjail_application_permissions(GHashTable *appinfo);

/* ------------------------------------------------------------------------- *
 * MAIN
 * ------------------------------------------------------------------------- */

int main(int argc, char **argv);

/* ========================================================================= *
 * MAIN
 * ========================================================================= */

static const struct option long_options[] = {
    {"help",         no_argument,       NULL, 'h'},
    {"verbose",      no_argument,       NULL, 'v'},
    {"quiet",        no_argument,       NULL, 'q'},
    {"version",      no_argument,       NULL, 'V'},
    {"desktop",      required_argument, NULL, 'd'},
    {0, 0, 0, 0}
};
static const char short_options[] = "hvqVd";

static const char usage_template[] = ""
"NAME\n"
"  %s  --  command line utility for launching sandboxed application\n"
"\n"
"SYNOPSIS\n"
"  %s <option> [--] <application_path> [args]\n"
"\n"
"DESCRIPTION\n"
"  This tool gets application lauch permissions from sailjaild and\n"
"  then starts the application in appropriate firejail sandbox.\n"
"\n"
"OPTIONS\n"
"  -h --help\n"
"        Writes this help text to stdout\n"
"  -V --version\n"
"        Writes tool version to stdout.\n"
"  -q --quiet\n"
"        Makes tool less verbose.\n"
"  -v --verbose\n"
"        Makes tool more verbose.\n"
"  -d --desktop=<desktop>\n"
"        Define application file instead of using heuristics based\n"
"        on path to launched application\n"
"\n"
"EXAMPLES\n"
"  %s -- /usr/bin/bar\n"
"        Launch application bar using permissions from bar.desktop\n"
"  %s -d org.foo.bar -- /usr/bin/bar\n"
"        Launch application bar using permissions from org.foo.bar.desktop\n"
"\n"
"COPYRIGHT\n"
"  Copyright (c) 2021 Open Mobile Platform LLC.\n"
"\n"
"SEE ALSO\n"
"  sailjaild\n"
"\n";

static const char usage_hint[] = "(use --help for instructions)\n";

static void
client_usage(const char *progname)
{
    fprintf(stdout, usage_template,
            progname,
            progname,
            progname,
            progname);
}

gchar **
prompt_permissions(GDBusConnection *connection, const char *application)
{
    GError *err = NULL;
    GVariant *reply = NULL;
    gchar **permissions = NULL;

    reply = g_dbus_connection_call_sync(connection,
                                        PERMISSIONMGR_SERVICE,
                                        PERMISSIONMGR_OBJECT,
                                        PERMISSIONMGR_INTERFACE,
                                        PERMISSIONMGR_METHOD_PROMPT,
                                        g_variant_new("(s)", application),
                                        NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &err);
    if( err || !reply ) {
        fprintf(stderr, "%s.%s(%s): failed: %s\n",
                PERMISSIONMGR_INTERFACE, PERMISSIONMGR_METHOD_PROMPT,
                application, err ? err->message : "no reply");
        goto EXIT;
    }

    g_variant_get(reply, "(^as)", &permissions);
    if( !permissions ) {
        fprintf(stderr, "%s.%s(%s): failed: %s\n",
                PERMISSIONMGR_INTERFACE, PERMISSIONMGR_METHOD_PROMPT,
                application, "invalid reply");
    }

EXIT:
    if( reply )
        g_variant_unref(reply);
    g_clear_error(&err);
    return permissions;
}

const char *
appinfo_get_string(GHashTable *appinfo, const char *key)
{
    const char *value = NULL;
    GVariant *variant = g_hash_table_lookup(appinfo, key);
    if( variant) {
        const GVariantType *type = g_variant_get_type(variant);
        if( g_variant_type_equal(type, G_VARIANT_TYPE_STRING) )
            value = g_variant_get_string(variant, NULL);
    }
    return value;
}

const char **
appinfo_get_strv(GHashTable *appinfo, const char *key)
{
    const char **value = NULL;
    GVariant *variant = g_hash_table_lookup(appinfo, key);
    if( variant) {
        const GVariantType *type = g_variant_get_type(variant);
        if( g_variant_type_equal(type, G_VARIANT_TYPE("as")) )
            value = g_variant_get_strv(variant, NULL);
    }
    return value;
}

const char *
appinfo_deskop_exec(GHashTable *appinfo)
{
    return appinfo_get_string(appinfo, DESKTOP_KEY_EXEC);
}

const char *
appinfo_sailjail_organization_name(GHashTable *appinfo)
{
    return appinfo_get_string(appinfo, SAILJAIL_KEY_ORGANIZATION_NAME);
}

const char *
appinfo_sailjail_application_name(GHashTable *appinfo)
{
    return appinfo_get_string(appinfo, SAILJAIL_KEY_APPLICATION_NAME);
}

const char **
appinfo_sailjail_application_permissions(GHashTable *appinfo)
{
    return appinfo_get_strv(appinfo, SAILJAIL_KEY_PERMISSIONS);
}

const char *
appinfo_maemo_service(GHashTable *appinfo)
{
    return appinfo_get_string(appinfo, MAEMO_KEY_SERVICE);
}

const char *
appinfo_maemo_method(GHashTable *appinfo)
{
    return appinfo_get_string(appinfo, MAEMO_KEY_METHOD);
}

GHashTable *
query_appinfo(GDBusConnection *connection, const char *application)
{
    GHashTable *appinfo = NULL;
    GError     *err     = NULL;
    GVariant   *reply   = NULL;

    reply = g_dbus_connection_call_sync(connection,
                                        PERMISSIONMGR_SERVICE,
                                        PERMISSIONMGR_OBJECT,
                                        PERMISSIONMGR_INTERFACE,
                                        PERMISSIONMGR_METHOD_GET_APPINFO,
                                        g_variant_new("(s)", application),
                                        NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &err);
    if( err || !reply ) {
        fprintf(stderr, "%s.%s(%s): failed: %s\n",
                PERMISSIONMGR_INTERFACE, PERMISSIONMGR_METHOD_PROMPT,
                application, err ? err->message : "no reply");
        goto EXIT;
    }
    GVariantIter  *iter_array = NULL;
    g_variant_get(reply, "(a{sv})", &iter_array);
    if( !iter_array )
        goto EXIT;

    appinfo = g_hash_table_new_full(g_str_hash,
                                    g_str_equal,
                                    g_free,
                                    (GDestroyNotify)g_variant_unref);

    if( iter_array ) {
        const char *key = NULL;
        GVariant   *val = NULL;
        while( g_variant_iter_loop (iter_array, "{&sv}", &key, &val) ) {
            const GVariantType *type = g_variant_get_type(val);
            if( g_variant_type_equal(type, G_VARIANT_TYPE_BOOLEAN) )
                printf("%s=%s\n", key, g_variant_get_boolean(val) ? "true" : "false");
            else if( g_variant_type_equal(type, G_VARIANT_TYPE_STRING) )
                printf("%s='%s'\n", key, g_variant_get_string(val, NULL));
            else
                printf("%s=%s@%p\n", key, (char *)type, val);
            g_hash_table_insert(appinfo, g_strdup(key), g_variant_ref(val));
        }
        g_variant_iter_free(iter_array);
    }

EXIT:
    if( reply )
        g_variant_unref(reply);
    g_clear_error(&err);
    return appinfo;
}

static bool
validate_args(int argc, char **argv, const char *exec)
{
    // FIXME: use existing impl
    return true;
}

static int
client_exec(const char *desktop, int argc, char **argv)
{
    GError           *err         = NULL;
    gchar            *application = NULL;
    GDBusConnection  *connection  = NULL;
    gchar           **granted     = NULL;
    GHashTable       *appinfo     = NULL;

    if( !(application = path_to_desktop_name(desktop)) )
        goto EXIT;

    if( !(connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err)) )
        goto EXIT;

    if( !(granted = prompt_permissions(connection, application)) )
        goto EXIT;

    for( int i = 0; granted[i]; ++i )
        printf("GRANTED += %s\n", granted[i]);

    if( !(appinfo = query_appinfo(connection, application)) )
        goto EXIT;

    printf("APPINFO = %p\n", appinfo);

    const char  *exec        = appinfo_deskop_exec(appinfo);
    const char  *org_name    = appinfo_sailjail_organization_name(appinfo);
    const char  *app_name    = appinfo_sailjail_application_name(appinfo);
    const char **permissions = appinfo_sailjail_application_permissions(appinfo);
    const char  *service     = appinfo_maemo_service(appinfo);
    const char  *method      = appinfo_maemo_method(appinfo);

    printf("exec = %s\n", exec);
    printf("org_name = %s\n", org_name);
    printf("app_name = %s\n", app_name);
    printf("service = %s\n", service);
    printf("method = %s\n", method);
    if( permissions ) {
        for( int i = 0; permissions[i]; ++i )
            printf("permissions += %s\n", permissions[i]);
    }

    if( !exec ) {
        fprintf(stderr, "Exec line not defined");
        goto EXIT;
    }

    if( !validate_args(argc, argv, exec) ) {
        fprintf(stderr, "Command line does not match template");
        goto EXIT;
    }

// QUARANTINE     const char *home = getenv("HOME");

    stringset_t *sailjail = stringset_create();
    stringset_add_item(sailjail, "/usr/bin/firejail");
    stringset_add_item_fmt(sailjail, "--private-bin=%s", path_basename(*argv));
    stringset_add_item_fmt(sailjail, "--whitelist=/usr/share/%s", path_basename(*argv));
    stringset_add_item_fmt(sailjail, "--whitelist=%s", desktop);

    // Legacy app share dir
    stringset_add_item_fmt(sailjail, "--whitelist=${HOME}/.local/share/%s", path_basename(*argv));

    stringset_add_item_fmt(sailjail, "--mkdir=${HOME}/.cache/%s/%s", org_name, app_name);
    stringset_add_item_fmt(sailjail, "--whitelist=${HOME}/.cache/%s/%s", org_name, app_name);

    stringset_add_item_fmt(sailjail, "--mkdir=${HOME}/.local/share/%s/%s", org_name, app_name);
    stringset_add_item_fmt(sailjail, "--whitelist=${HOME}/.local/share/%s/%s", org_name, app_name);

    stringset_add_item_fmt(sailjail, "--mkdir=${HOME}/.config/%s/%s", org_name, app_name);
    stringset_add_item_fmt(sailjail, "--whitelist=${HOME}/.config/%s/%s", org_name, app_name);

    stringset_add_item_fmt(sailjail, "--dbus-user.own=%s.%s", org_name, app_name);
    stringset_add_item_fmt(sailjail, "--dbus-user.own=%s", service);

    auto void add_permission(const char *name) {
        gchar *path = path_from_permission_name(name);
        if( access(path, R_OK) == 0 )
            stringset_add_item_fmt(sailjail, "--profile=%s", path);
        g_free(path);
    }

    auto void add_profile(const char *name) {
        gchar *path = path_from_profile_name(name);
        if( access(path, R_OK) == 0 )
            stringset_add_item_fmt(sailjail, "--profile=%s", path);
        g_free(path);
    }

    add_profile(path_basename(*argv));
    for( size_t i = 0; granted[i]; ++i )
        add_permission(granted[i]);
    add_permission("Base");

    stringset_add_item(sailjail, "--");

    GArray *array = g_array_new(true, false, sizeof(gchar *));
    for( const GList *iter = stringset_list(sailjail); iter; iter = iter->next ) {
        const char *arg = iter->data;
        g_array_append_val(array, arg);
    }
    for( int i = 0; i < argc; ++i )
        g_array_append_val(array, argv[i]);

    char **args = (char **)g_array_free(array, false);

    for( int i = 0; args[i]; ++i )
        printf("arg[%02d] = %s\n", i, args[i]);

    // FIXME: handle "Privileged"

    errno = 0;
    fflush(0);
    execv(*args, args);
    printf("%s: exec failed: %m\n", *args);

    stringset_delete(sailjail);
    g_free(args);

EXIT:
    g_clear_error(&err);
    if( appinfo )
        g_hash_table_unref(appinfo);
    g_strfreev(granted);
    if( connection )
        g_object_unref(connection);
    g_free(application);

    return EXIT_FAILURE;
}

#if 0
/usr/bin/firejail
 --debug
 --profile=/etc/sailjail/permissions/Accounts.permission
 --profile=/etc/sailjail/permissions/Contacts.permission
 --profile=/etc/sailjail/permissions/Phone.permission
 --profile=/etc/sailjail/permissions/Email.permission
 --profile=/etc/sailjail/permissions/WebView.permission
 --profile=/etc/sailjail/permissions/Internet.permission
 --profile=/etc/sailjail/permissions/AppLaunch.permission
 --profile=/etc/sailjail/permissions/Calendar.permission
 --profile=/etc/sailjail/permissions/jolla-email.profile
 --profile=/etc/sailjail/permissions/Base.permission
 --whitelist=/usr/share/jolla-email
 --whitelist=/usr/share/applications/jolla-email.desktop
 --whitelist=/home/defaultuser/.local/share/jolla-email
 --mkdir=${HOME}/.cache/com.jolla/email
 --whitelist=${HOME}/.cache/com.jolla/email
 --mkdir=${HOME}/.local/share/com.jolla/email
 --whitelist=${HOME}/.local/share/com.jolla/email
 --mkdir=${HOME}/.config/com.jolla/email
 --whitelist=${HOME}/.config/com.jolla/email
 --dbus-user=filter
 --dbus-user.log
 --dbus-user.own=com.jolla.email
 --private-bin=jolla-email
 --
 /usr/bin/jolla-email
#endif

int
client_main(int argc, char **argv)
{
    int         exit_code = EXIT_FAILURE;
    const char *progname  = path_basename(*argv);
    config_t   *config    = config_create();
    gchar      *desktop   = NULL;

    /* Handle options */
    for( ;; ) {
        int opt = getopt_long(argc, argv, short_options, long_options, 0);

        if( opt == -1 )
            break;

        switch( opt ) {
        case 'h':
            client_usage(progname);
            exit_code = EXIT_SUCCESS;
            goto EXIT;
        case 'v':
            log_set_level(log_get_level() + 1);
            break;
        case 'q':
            log_set_level(log_get_level() - 1);
            break;
        case 'V':
            printf("%s\n", VERSION);
            exit_code = EXIT_SUCCESS;
            goto EXIT;
        case 'd':
            desktop = path_from_desktop_name(optarg);
            break;
        case '?':
            fputs(usage_hint, stderr);
            goto EXIT;
        }
    }

    argv += optind;
    argc -= optind;

    if( argc < 1 ) {
        fprintf(stderr, "No application to launch given\n%s", usage_hint);
        goto EXIT;
    }

    const char *binary = *argv;
    if( *binary != '/' ) {
        fprintf(stderr, "%s: is not an absolute path\n", binary);
        goto EXIT;
    }

    if( access(binary, R_OK|X_OK) == -1 ) {
        fprintf(stderr, "%s: is not executable: %m\n", binary);
        goto EXIT;
    }

    if( !desktop ) {
        if( !(desktop = path_from_desktop_name(binary)) )
            goto EXIT;
    }

    if( access(desktop, R_OK) == -1 ) {
        fprintf(stderr, "%s: is not readable: %m\n", desktop);
        goto EXIT;
    }

    exit_code = client_exec(desktop, argc, argv);

EXIT:
    config_delete(config);
    log_debug("exit %d", exit_code);
    return exit_code;
}

int
main(int argc, char **argv)
{
    return client_main(argc, argv);
}

# define PERMISSIONMGR_SERVICE                 "org.sailfishos.sailjaild1"
# define PERMISSIONMGR_INTERFACE               "org.sailfishos.sailjaild1"
# define PERMISSIONMGR_OBJECT                  "/org/sailfishos/sailjaild1"
# define PERMISSIONMGR_METHOD_QUIT             "Quit"
# define PERMISSIONMGR_METHOD_PROMPT           "PromptLaunchPermissions"
# define PERMISSIONMGR_METHOD_QUERY            "QueryLaunchPermissions"
# define PERMISSIONMGR_METHOD_GET_APPLICATIONS "GetApplications"
# define PERMISSIONMGR_METHOD_GET_APPINFO      "GetAppInfo"
# define PERMISSIONMGR_METHOD_GET_LICENSE      "GetLicenseAgreed"
# define PERMISSIONMGR_METHOD_SET_LICENSE      "SetLicenseAgreed"
# define PERMISSIONMGR_METHOD_GET_LAUNCHABLE   "GetLaunchAllowed"
# define PERMISSIONMGR_METHOD_SET_LAUNCHABLE   "SetLaunchAllowed"
# define PERMISSIONMGR_METHOD_GET_GRANTED      "GetGrantedPermissions"
# define PERMISSIONMGR_METHOD_SET_GRANTED      "SetGrantedPermissions"
# define PERMISSIONMGR_SIGNAL_APP_ADDED        "ApplicationAdded"
# define PERMISSIONMGR_SIGNAL_APP_CHANGED      "ApplicationChanged"
# define PERMISSIONMGR_SIGNAL_APP_REMOVED      "ApplicationRemoved"
