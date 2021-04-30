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

#include "appinfo.h"

#include "applications.h"
#include "control.h"
#include "stringset.h"
#include "logging.h"
#include "util.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

#include <gio/gio.h>

/* ========================================================================= *
 * Types
 * ========================================================================= */

typedef enum
{
    APPINFO_STATE_UNSET,
    APPINFO_STATE_VALID,
    APPINFO_STATE_INVALID,
    APPINFO_STATE_DELETED,
} appinfo_state_t;

static const char * const appinfo_state_name[] =
{
    [APPINFO_STATE_UNSET]   = "UNSET",
    [APPINFO_STATE_VALID]   = "VALID",
    [APPINFO_STATE_INVALID] = "INVALID",
    [APPINFO_STATE_DELETED] = "DELETED",
};

typedef struct
{
    const char *key;
    void (*set)(appinfo_t *, const char *);
} appinfo_parser_t;

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * APPINFO
 * ------------------------------------------------------------------------- */

static void  appinfo_ctor      (appinfo_t *self, applications_t *applications, const gchar *id);
static void  appinfo_dtor      (appinfo_t *self);
appinfo_t   *appinfo_create    (applications_t *applications, const gchar *id);
void         appinfo_delete    (appinfo_t *self);
void         appinfo_delete_cb (void *self);
gboolean     appinfo_equal     (const appinfo_t *self, const appinfo_t *that);
gboolean     appinfo_equal_cb  (gconstpointer a, gconstpointer b);
guint        appinfo_hash      (const appinfo_t *self);
guint        appinfo_hash_cb   (gconstpointer key);
GVariant    *appinfo_to_variant(const appinfo_t *self);
gchar       *appinfo_to_string (const appinfo_t *self);

/* ------------------------------------------------------------------------- *
 * APPINFO_ATTRIBUTE
 * ------------------------------------------------------------------------- */

bool            appinfo_valid       (const appinfo_t *self);
control_t      *appinfo_control     (const appinfo_t *self);
applications_t *appinfo_applications(const appinfo_t *self);
const gchar    *appinfo_id          (const appinfo_t *self);

/* ------------------------------------------------------------------------- *
 * APPINFO_PROPERTY
 * ------------------------------------------------------------------------- */

static void             appinfo_set_dirty            (appinfo_t *self);
static bool             appinfo_clear_dirty          (appinfo_t *self);
static appinfo_state_t  appinfo_get_state            (const appinfo_t *self);
static void             appinfo_set_state            (appinfo_t *self, appinfo_state_t state);
const gchar            *appinfo_get_name             (const appinfo_t *self);
const gchar            *appinfo_get_type             (const appinfo_t *self);
const gchar            *appinfo_get_icon             (const appinfo_t *self);
const gchar            *appinfo_get_exec             (const appinfo_t *self);
bool                    appinfo_get_no_display       (const appinfo_t *self);
const gchar            *appinfo_get_service          (const appinfo_t *self);
const gchar            *appinfo_get_object           (const appinfo_t *self);
const gchar            *appinfo_get_method           (const appinfo_t *self);
const gchar            *appinfo_get_organization_name(const appinfo_t *self);
const gchar            *appinfo_get_application_name (const appinfo_t *self);
void                    appinfo_set_name             (appinfo_t *self, const gchar *name);
void                    appinfo_set_type             (appinfo_t *self, const gchar *type);
void                    appinfo_set_icon             (appinfo_t *self, const gchar *icon);
void                    appinfo_set_exec             (appinfo_t *self, const gchar *exec);
void                    appinfo_set_no_display       (appinfo_t *self, bool no_display);
void                    appinfo_set_service          (appinfo_t *self, const gchar *service);
void                    appinfo_set_object           (appinfo_t *self, const gchar *object);
void                    appinfo_set_method           (appinfo_t *self, const gchar *method);
void                    appinfo_set_organization_name(appinfo_t *self, const gchar *organization_name);
void                    appinfo_set_application_name (appinfo_t *self, const gchar *application_name);

/* ------------------------------------------------------------------------- *
 * APPINFO_PERMISSIONS
 * ------------------------------------------------------------------------- */

bool         appinfo_has_permission      (const appinfo_t *self, const gchar *perm);
stringset_t *appinfo_get_permissions     (const appinfo_t *self);
bool         appinfo_evaluate_permissions(appinfo_t *self);
void         appinfo_set_permissions     (appinfo_t *self, const stringset_t *in);
void         appinfo_clear_permissions   (appinfo_t *self);

/* ------------------------------------------------------------------------- *
 * APPINFO_PARSE
 * ------------------------------------------------------------------------- */

bool appinfo_parse_desktop(appinfo_t *self);

/* ========================================================================= *
 * APPINFO
 * ========================================================================= */

/* Ref: data merged from all desktop files under /usr/share/applications
 *
 * [Desktop Entry]
 * Type=Application
 * Name=Settings
 * Exec=/usr/bin/sailjail -p voicecall-ui.desktop /usr/bin/voicecall-ui
 * NoDisplay=true
 * Icon=icon-launcher-settings
 * Comment=Sailfish MimeType Handler for Webcal URL
 * X-Desktop-File-Install-Version=0.26
 * X-MeeGo-Logical-Id=settings-ap-name
 * X-MeeGo-Translation-Catalog=settings
 * X-Maemo-Service=com.jolla.settings
 * X-Maemo-Object-Path=/com/jolla/settings/ui
 * X-Maemo-Method=com.jolla.settings.ui.importWebcal
 * MimeType=x-scheme-handler/webcal;x-scheme-handler/webcals;
 * Version=1.0
 * X-Maemo-Fixed-Args=application/x-vpnc
 *
 * [X-Sailjail]
 * Permissions=Phone;CallRecordings;Contacts;Bluetooth;Privileged;Sharing
 * OrganizationName=com.jolla
 * ApplicationName=voicecall
 */

struct appinfo_t
{
    // uplink
    applications_t  *anf_applications;

    gchar           *anf_appname;
    appinfo_state_t  anf_state;
    time_t           anf_dt_ctime;
    bool             anf_dirty;

    // desktop properties
    gchar           *anf_dt_name;       // DESKTOP_KEY_NAME
    gchar           *anf_dt_type;       // DESKTOP_KEY_TYPE
    gchar           *anf_dt_icon;       // DESKTOP_KEY_ICON
    gchar           *anf_dt_exec;       // DESKTOP_KEY_EXEC
    bool             anf_dt_no_display; // DESKTOP_KEY_NO_DISPLAY

    // maemo properties
    gchar           *anf_mo_service;    // MAEMO_KEY_SERVICE
    gchar           *anf_mo_object;     // MAEMO_KEY_OBJECT
    gchar           *anf_mo_method;     // MAEMO_KEY_METHOD

    // sailjail properties
    gchar           *anf_sj_organization_name; // SAILJAIL_KEY_ORGANIZATION_NAME
    gchar           *anf_sj_application_name;  // SAILJAIL_KEY_APPLICATION_NAME
    stringset_t     *anf_sj_permissions_in;    // SAILJAIL_KEY_PERMISSIONS
    stringset_t     *anf_sj_permissions_out;
};

static void
appinfo_ctor(appinfo_t *self, applications_t *applications, const gchar *id)
{
    self->anf_applications         = applications;
    self->anf_appname              = g_strdup(id);

    self->anf_state                = APPINFO_STATE_UNSET;
    self->anf_dt_ctime             = -1;
    self->anf_dirty                = false;

    self->anf_dt_name              = NULL;
    self->anf_dt_type              = NULL;
    self->anf_dt_icon              = NULL;
    self->anf_dt_exec              = NULL;
    self->anf_dt_no_display        = false;

    self->anf_mo_service           = NULL;
    self->anf_mo_object            = NULL;
    self->anf_mo_method            = NULL;

    self->anf_sj_permissions_in    = stringset_create();
    self->anf_sj_permissions_out   = stringset_create();
    self->anf_sj_organization_name = NULL;
    self->anf_sj_application_name  = NULL;

    log_info("appinfo(%s): create", appinfo_id(self));
}

static void
appinfo_dtor(appinfo_t *self)
{
    log_info("appinfo(%s): delete", appinfo_id(self));

    appinfo_set_name(self, NULL);
    appinfo_set_type(self, NULL);
    appinfo_set_icon(self, NULL);
    appinfo_set_exec(self, NULL);

    appinfo_set_service(self, NULL);
    appinfo_set_object(self, NULL);
    appinfo_set_method(self, NULL);

    appinfo_set_organization_name(self, NULL);
    appinfo_set_application_name(self, NULL);
    stringset_delete_at(&self->anf_sj_permissions_in);
    stringset_delete_at(&self->anf_sj_permissions_out);

    change_string(&self->anf_appname, NULL);
    self->anf_applications = NULL;
}

appinfo_t *
appinfo_create(applications_t *applications, const gchar *id)
{
    appinfo_t *self = g_malloc0(sizeof *self);
    appinfo_ctor(self, applications, id);
    return self;
}

void
appinfo_delete(appinfo_t *self)
{
    if( self ) {
        appinfo_dtor(self);
        g_free(self);
    }
}

void
appinfo_delete_cb(void *self)
{
    appinfo_delete(self);
}

gboolean
appinfo_equal(const appinfo_t *self, const appinfo_t *that)
{
    return g_str_equal(appinfo_id(self), appinfo_id(that));
}

gboolean
appinfo_equal_cb(gconstpointer a, gconstpointer b)
{
    return appinfo_equal(a, b);
}

guint
appinfo_hash(const appinfo_t *self)
{
    return g_str_hash(appinfo_id(self));
}

guint
appinfo_hash_cb(gconstpointer key)
{
    return appinfo_hash(key);
}

GVariant *
appinfo_to_variant(const appinfo_t *self)
{
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

    if( self ) {
        g_variant_builder_add(builder, "{sv}", "Id",
                              g_variant_new_string(appinfo_id(self)));

        /* Desktop properties
         */
        g_variant_builder_add(builder, "{sv}", DESKTOP_KEY_NAME,
                              g_variant_new_string(appinfo_get_name(self)));

        g_variant_builder_add(builder, "{sv}", DESKTOP_KEY_TYPE,
                              g_variant_new_string(appinfo_get_type(self)));

        g_variant_builder_add(builder, "{sv}", DESKTOP_KEY_ICON,
                              g_variant_new_string(appinfo_get_icon(self)));

        g_variant_builder_add(builder, "{sv}", DESKTOP_KEY_EXEC,
                              g_variant_new_string(appinfo_get_exec(self)));

        g_variant_builder_add(builder, "{sv}", DESKTOP_KEY_NO_DISPLAY,
                              g_variant_new_boolean(appinfo_get_no_display(self)));

        /* Maemo properties
         */
        g_variant_builder_add(builder, "{sv}", MAEMO_KEY_SERVICE,
                              g_variant_new_string(appinfo_get_service(self)));

        g_variant_builder_add(builder, "{sv}", MAEMO_KEY_OBJECT,
                              g_variant_new_string(appinfo_get_object(self)));

        g_variant_builder_add(builder, "{sv}", MAEMO_KEY_METHOD,
                              g_variant_new_string(appinfo_get_method(self)));

        /* Sailjail properties
         */
        g_variant_builder_add(builder, "{sv}", SAILJAIL_KEY_ORGANIZATION_NAME,
                              g_variant_new_string(appinfo_get_organization_name(self)));

        g_variant_builder_add(builder, "{sv}", SAILJAIL_KEY_APPLICATION_NAME,
                              g_variant_new_string(appinfo_get_application_name(self)));

        g_variant_builder_add(builder, "{sv}", SAILJAIL_KEY_PERMISSIONS,
                              stringset_to_variant(appinfo_get_permissions(self)));
    }

    GVariant *variant = g_variant_builder_end(builder);
    g_variant_builder_unref(builder);

    return variant;
}

gchar *
appinfo_to_string(const appinfo_t *self)
{
    gchar    *string  = NULL;
    GVariant *variant = appinfo_to_variant(self);
    if( variant ) {
        string = g_variant_print(variant, false);
        g_variant_unref(variant);
    }
    return string;
}

/* ------------------------------------------------------------------------- *
 * APPINFO_ATTRIBUTE
 * ------------------------------------------------------------------------- */

bool
appinfo_valid(const appinfo_t *self)
{
    return appinfo_get_state(self) == APPINFO_STATE_VALID;
}

control_t *
appinfo_control(const appinfo_t *self)
{
    return applications_control(appinfo_applications(self));
}

applications_t *
appinfo_applications(const appinfo_t *self)
{
    return self->anf_applications;
}

const gchar *
appinfo_id(const appinfo_t *self)
{
    return self->anf_appname;
}

/* ------------------------------------------------------------------------- *
 * APPINFO_PROPERTY
 * ------------------------------------------------------------------------- */

static const char appinfo_unknown[] = "unknown";

static void
appinfo_set_dirty(appinfo_t *self)
{
    self->anf_dirty = true;
}

static bool
appinfo_clear_dirty(appinfo_t *self)
{
    bool was_dirty = self->anf_dirty;
    self->anf_dirty = false;
    return was_dirty;
}

static appinfo_state_t
appinfo_get_state(const appinfo_t *self)
{
    return self ? self->anf_state : APPINFO_STATE_DELETED;
}

static void
appinfo_set_state(appinfo_t *self, appinfo_state_t state)
{
    if( self->anf_state != state ) {
        log_debug("appinfo(%s): state: %s -> %s",
                  appinfo_id(self),
                  appinfo_state_name[self->anf_state],
                  appinfo_state_name[state]);
        self->anf_state = state;
        appinfo_set_dirty(self);
    }
}

/* - - - - - - - - - - - - - - - - - - - *
 * Getters
 * - - - - - - - - - - - - - - - - - - - */

const gchar *
appinfo_get_name(const appinfo_t *self)
{
    return self->anf_dt_name ?: appinfo_unknown;
}

const gchar *
appinfo_get_type(const appinfo_t *self)
{
    return self->anf_dt_type ?: appinfo_unknown;
}

const gchar *
appinfo_get_icon(const appinfo_t *self)
{
    return self->anf_dt_icon ?: appinfo_unknown;
}

const gchar *
appinfo_get_exec(const appinfo_t *self)
{
    return self->anf_dt_exec ?: appinfo_unknown;
}

bool
appinfo_get_no_display(const appinfo_t *self)
{
    return self->anf_dt_no_display;
}

const gchar *
appinfo_get_service(const appinfo_t *self)
{
    return self->anf_mo_service ?: appinfo_unknown;
}

const gchar *
appinfo_get_object(const appinfo_t *self)
{
    return self->anf_mo_object ?: appinfo_unknown;
}

const gchar *
appinfo_get_method(const appinfo_t *self)
{
    return self->anf_mo_method ?: appinfo_unknown;
}

const gchar *
appinfo_get_organization_name(const appinfo_t *self)
{
    return self->anf_sj_organization_name ?: appinfo_unknown;
}

const gchar *
appinfo_get_application_name(const appinfo_t *self)
{
    return self->anf_sj_application_name ?: appinfo_unknown;
}

/* - - - - - - - - - - - - - - - - - - - *
 * Setters
 * - - - - - - - - - - - - - - - - - - - */

void
appinfo_set_name(appinfo_t *self, const gchar *name)
{
    if( change_string(&self->anf_dt_name, name) )
        appinfo_set_dirty(self);
}

void
appinfo_set_type(appinfo_t *self, const gchar *type)
{
    if( change_string(&self->anf_dt_type, type) )
        appinfo_set_dirty(self);
}

void
appinfo_set_icon(appinfo_t *self, const gchar *icon)
{
    if( change_string(&self->anf_dt_icon, icon) )
        appinfo_set_dirty(self);
}

void
appinfo_set_exec(appinfo_t *self, const gchar *exec)
{
    if( change_string(&self->anf_dt_exec, exec) )
        appinfo_set_dirty(self);
}

void
appinfo_set_no_display(appinfo_t *self, bool no_display)
{
    if( change_boolean(&self->anf_dt_no_display, no_display) )
        appinfo_set_dirty(self);
}

void
appinfo_set_service(appinfo_t *self, const gchar *service)
{
    if( change_string(&self->anf_mo_service, service) )
        appinfo_set_dirty(self);
}

void
appinfo_set_object(appinfo_t *self, const gchar *object)
{
    if( change_string(&self->anf_mo_object, object) )
        appinfo_set_dirty(self);
}

void
appinfo_set_method(appinfo_t *self, const gchar *method)
{
    if( change_string(&self->anf_mo_method, method) )
        appinfo_set_dirty(self);
}

void
appinfo_set_organization_name(appinfo_t *self, const gchar *organization_name)
{
    if( change_string(&self->anf_sj_organization_name, organization_name) )
        appinfo_set_dirty(self);
}

void
appinfo_set_application_name(appinfo_t *self, const gchar *application_name)
{
    if( change_string(&self->anf_sj_application_name, application_name) )
        appinfo_set_dirty(self);
}

/* ------------------------------------------------------------------------- *
 * APPINFO_PERMISSIONS
 * ------------------------------------------------------------------------- */

bool
appinfo_has_permission(const appinfo_t *self, const gchar *perm)
{
    return stringset_has_item(appinfo_get_permissions(self), perm);
}

stringset_t *
appinfo_get_permissions(const appinfo_t *self)
{
    return self->anf_sj_permissions_out;
}

bool
appinfo_evaluate_permissions(appinfo_t *self)
{
    bool changed = false;
    const stringset_t *mask = control_available_permissions(appinfo_control(self));
    stringset_t *temp = stringset_filter_in(self->anf_sj_permissions_in, mask);

    if( stringset_assign(self->anf_sj_permissions_out, temp) )
        changed = true;

    stringset_delete(temp);

    return changed;
}

void
appinfo_set_permissions(appinfo_t *self, const stringset_t *in)
{
    stringset_assign(self->anf_sj_permissions_in, in);
    if( appinfo_evaluate_permissions(self) )
        appinfo_set_dirty(self);
}

void
appinfo_clear_permissions(appinfo_t *self)
{
    if( stringset_clear(appinfo_get_permissions(self)) )
        appinfo_set_dirty(self);
}

/* ------------------------------------------------------------------------- *
 * APPINFO_PARSE
 * ------------------------------------------------------------------------- */

bool
appinfo_parse_desktop(appinfo_t *self)
{
    GKeyFile  *ini         = NULL;
    gchar     *path        = NULL;
    GError    *err         = NULL;
    gchar    **permissions = NULL;

    path = path_from_desktop_name(appinfo_id(self));

    /* Check if the file has changed since last parse */
    struct stat st = {};
    if( stat(path, &st) == -1 ) {
        log_warning("%s: could not stat: %m", path);
        if( errno == ENOENT )
            appinfo_set_state(self, APPINFO_STATE_DELETED);
        else
            appinfo_set_state(self, APPINFO_STATE_INVALID);
        goto EXIT;
    }

    if( self->anf_dt_ctime == st.st_ctime ) {
        /* Retain current state */
        goto EXIT;
    }

    self->anf_dt_ctime = st.st_ctime;

    /* Read file contents */
    if( access(path, R_OK) == -1 ) {
        log_warning("%s: not accessible: %m", path);
        appinfo_set_state(self, APPINFO_STATE_INVALID);
        goto EXIT;
    }

    ini = g_key_file_new();
    if( !keyfile_load(ini, path) ) {
        appinfo_set_state(self, APPINFO_STATE_INVALID);
        goto EXIT;
    }

    //log_debug("appinfo(%s): updating", appinfo_id(self));

    /* Parse desktop properties
     */
    gchar *tmp;

    tmp = keyfile_get_string(ini, DESKTOP_SECTION, DESKTOP_KEY_NAME, 0),
        appinfo_set_name(self, tmp),
        g_free(tmp);

    tmp = keyfile_get_string(ini, DESKTOP_SECTION, DESKTOP_KEY_TYPE, 0),
        appinfo_set_type(self, tmp),
        g_free(tmp);

    tmp = keyfile_get_string(ini, DESKTOP_SECTION, DESKTOP_KEY_ICON, 0),
        appinfo_set_icon(self, tmp),
        g_free(tmp);

    tmp = keyfile_get_string(ini, DESKTOP_SECTION, DESKTOP_KEY_EXEC, 0),
        appinfo_set_exec(self, tmp),
        g_free(tmp);

    appinfo_set_no_display(self, keyfile_get_boolean(ini, DESKTOP_SECTION,
                                                     DESKTOP_KEY_NO_DISPLAY,
                                                     false));

    /* Parse maemo properties
     */
    tmp = keyfile_get_string(ini, MAEMO_SECTION, MAEMO_KEY_SERVICE, 0),
        appinfo_set_service(self, tmp),
        g_free(tmp);

    tmp = keyfile_get_string(ini, MAEMO_SECTION, MAEMO_KEY_OBJECT, 0),
        appinfo_set_object(self, tmp),
        g_free(tmp);

    tmp = keyfile_get_string(ini, MAEMO_SECTION, MAEMO_KEY_METHOD, 0),
        appinfo_set_method(self, tmp),
        g_free(tmp);

    /* Parse sailjail properties
     */
    const gchar *group = SAILJAIL_SECTION_PRIMARY;
    if( !g_key_file_has_group(ini, group) )
        group = SAILJAIL_SECTION_SECONDARY;

    tmp = keyfile_get_string(ini, group, SAILJAIL_KEY_ORGANIZATION_NAME, 0),
        appinfo_set_organization_name(self, tmp),
        g_free(tmp);

    tmp = keyfile_get_string(ini, group, SAILJAIL_KEY_APPLICATION_NAME, 0),
        appinfo_set_application_name(self, tmp),
        g_free(tmp);

    stringset_t *set = keyfile_get_stringset(ini, group, SAILJAIL_KEY_PERMISSIONS);
    appinfo_set_permissions(self, set);
    stringset_delete(set);

    /* Validate */
    if( appinfo_get_name(self) != appinfo_unknown &&
        appinfo_get_type(self) != appinfo_unknown &&
        appinfo_get_exec(self) != appinfo_unknown )
        appinfo_set_state(self, APPINFO_STATE_VALID);
    else
        appinfo_set_state(self, APPINFO_STATE_INVALID);

EXIT:
    g_clear_error(&err);
    g_strfreev(permissions);
    if( ini )
        g_key_file_unref(ini);
    g_free(path);

    return appinfo_clear_dirty(self);
}
