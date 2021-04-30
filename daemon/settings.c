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

#include "settings.h"

#include "util.h"
#include "logging.h"
#include "stringset.h"
#include "control.h"
#include "stringset.h"
#include "appinfo.h"

#include <glib.h>

/* ========================================================================= *
 * Types
 * ========================================================================= */

static const char * const app_allowed_name[] = {
    [APP_ALLOWED_UNSET] = "UNSET",
    [APP_ALLOWED_ALWAYS]= "ALWAYS",
    [APP_ALLOWED_NEVER] = "NEVER",
};

static const char * const app_agreed_name[] = {
    [APP_AGREED_UNSET] = "UNSET",
    [APP_AGREED_YES]   = "YES",
    [APP_AGREED_NO]    = "NO",
};

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * SETTINGS
 * ------------------------------------------------------------------------- */

static void  settings_ctor     (settings_t *self, const config_t *config, control_t *control);
static void  settings_dtor     (settings_t *self);
settings_t  *settings_create   (const config_t *config, control_t *control);
void         settings_delete   (settings_t *self);
void         settings_delete_at(settings_t **pself);
void         settings_delete_cb(void *self);

/* ------------------------------------------------------------------------- *
 * SETTINGS_ATTRIBUTES
 * ------------------------------------------------------------------------- */

static control_t *settings_control    (const settings_t *self);
appsettings_t    *settings_appsettings(settings_t *self, uid_t uid, const char *app);
static bool       settings_initialized(const settings_t *self);

/* ------------------------------------------------------------------------- *
 * SETTINGS_USERSETTINGS
 * ------------------------------------------------------------------------- */

usersettings_t *settings_get_usersettings   (const settings_t *self, uid_t uid);
usersettings_t *settings_add_usersettings   (settings_t *self, uid_t uid);
bool            settings_remove_usersettings(settings_t *self, uid_t uid);

/* ------------------------------------------------------------------------- *
 * SETTINGS_APPSETTING
 * ------------------------------------------------------------------------- */

appsettings_t *settings_get_appsettings   (const settings_t *self, uid_t uid, const char *appname);
appsettings_t *settings_add_appsettings   (settings_t *self, uid_t uid, const char *appname);
bool           settings_remove_appsettings(settings_t *self, uid_t uid, const char *appname);

/* ------------------------------------------------------------------------- *
 * SETTINGS_STORAGE
 * ------------------------------------------------------------------------- */

void            settings_load_all   (settings_t *self);
void            settings_save_all   (const settings_t *self);
void            settings_load_user  (settings_t *self, uid_t uid);
void            settings_save_user  (const settings_t *self, uid_t uid);
static void     settings_save_now   (settings_t *self);
static gboolean settings_save_cb    (gpointer aptr);
static void     settings_cancel_save(settings_t *self);
void            settings_save_later (settings_t *self, uid_t uid);

/* ------------------------------------------------------------------------- *
 * SETTINGS_RETHINK
 * ------------------------------------------------------------------------- */

void settings_rethink(settings_t *self);

/* ------------------------------------------------------------------------- *
 * SETTINGS_UTILITY
 * ------------------------------------------------------------------------- */

static gchar *settings_userdata_path(uid_t uid);

/* ------------------------------------------------------------------------- *
 * USERSETTINGS
 * ------------------------------------------------------------------------- */

static void     usersettings_ctor     (usersettings_t *self, settings_t *settings, uid_t uid);
static void     usersettings_dtor     (usersettings_t *self);
usersettings_t *usersettings_create   (settings_t *settings, uid_t uid);
void            usersettings_delete   (usersettings_t *self);
void            usersettings_delete_cb(void *self);

/* ------------------------------------------------------------------------- *
 * USERSETTINGS_ATTRIBUTES
 * ------------------------------------------------------------------------- */

static settings_t *usersettings_settings(const usersettings_t *self);
static uid_t       usersettings_uid     (const usersettings_t *self);
static control_t  *usersettings_control (const usersettings_t *self);

/* ------------------------------------------------------------------------- *
 * USERSETTINGS_APPSETTINGS
 * ------------------------------------------------------------------------- */

appsettings_t *usersettings_get_appsettings   (const usersettings_t *self, const gchar *appname);
appsettings_t *usersettings_add_appsettings   (usersettings_t *self, const gchar *appname);
bool           usersettings_remove_appsettings(usersettings_t *self, const gchar *appname);

/* ------------------------------------------------------------------------- *
 * USERSETTINGS_STORAGE
 * ------------------------------------------------------------------------- */

void usersettings_load(usersettings_t *self, const char *path);
void usersettings_save(const usersettings_t *self, const char *path);

/* ------------------------------------------------------------------------- *
 * USERSETTINGS_RETHINK
 * ------------------------------------------------------------------------- */

static void usersettings_rethink(usersettings_t *self);

/* ------------------------------------------------------------------------- *
 * APPSETTINGS
 * ------------------------------------------------------------------------- */

static void    appsettings_ctor     (appsettings_t *self, usersettings_t *usersettings, const char *appname);
static void    appsettings_dtor     (appsettings_t *self);
appsettings_t *appsettings_create   (usersettings_t *usersettings, const char *appname);
void           appsettings_delete   (appsettings_t *self);
void           appsettings_delete_cb(void *self);

/* ------------------------------------------------------------------------- *
 * APPSETTINGS_ATTRIBUTES
 * ------------------------------------------------------------------------- */

static control_t      *appsettings_control     (const appsettings_t *self);
static settings_t     *appsettings_settings    (const appsettings_t *self);
static usersettings_t *appsettings_usersettings(const appsettings_t *self);
static uid_t           appsettings_uid         (appsettings_t *self);
static const gchar    *appsettings_appname     (const appsettings_t *self);

/* ------------------------------------------------------------------------- *
 * APPSETTINGS_NOTIFY
 * ------------------------------------------------------------------------- */

static void appsettings_notify_change(appsettings_t *self);

/* ------------------------------------------------------------------------- *
 * APPSETTINGS_PROPERTIES
 * ------------------------------------------------------------------------- */

app_allowed_t      appsettings_get_allowed(const appsettings_t *self);
app_agreed_t       appsettings_get_agreed (const appsettings_t *self);
const stringset_t *appsettings_get_granted(appsettings_t *self);
void               appsettings_set_allowed(appsettings_t *self, app_allowed_t allowed);
void               appsettings_set_agreed (appsettings_t *self, app_agreed_t agreed);
void               appsettings_set_granted(appsettings_t *self, const stringset_t *granted);

/* ------------------------------------------------------------------------- *
 * APPSETTINGS_STORAGE
 * ------------------------------------------------------------------------- */

static void appsettings_decode(appsettings_t *self, GKeyFile *file);
static void appsettings_encode(const appsettings_t *self, GKeyFile *file);

/* ------------------------------------------------------------------------- *
 * APPSETTINGS_RETHINK
 * ------------------------------------------------------------------------- */

static void appsettings_rethink(appsettings_t *self);

/* ========================================================================= *
 * SETTINGS
 * ========================================================================= */

struct settings_t
{
    bool            stt_initialized;
    const config_t *stt_config;
    control_t      *stt_control;
    guint           stt_save_id;
    GHashTable     *stt_users;
    GHashTable     *stt_user_changes;
};

static void
settings_ctor(settings_t *self, const config_t *config, control_t *control)
{
    log_info("settings() created");
    self->stt_initialized  = false;
    self->stt_config       = config;
    self->stt_control      = control;
    self->stt_save_id      = 0;
    self->stt_users        = g_hash_table_new_full(g_direct_hash,
                                                   g_direct_equal,
                                                   NULL,
                                                   usersettings_delete_cb);
    self->stt_user_changes = g_hash_table_new(g_direct_hash, g_direct_equal);

    /* Get initial state */
    settings_load_all(self);

    /* Enable notifications */
    self->stt_initialized  = true;
}

static void
settings_dtor(settings_t *self)
{
    log_info("settings() deleted");
    self->stt_initialized  = false;

    if( self->stt_users ) {
        g_hash_table_unref(self->stt_users),
            self->stt_users = NULL;
    }

    if( self->stt_user_changes ) {
        g_hash_table_unref(self->stt_user_changes),
            self->stt_user_changes = 0;
    }
}

settings_t *
settings_create(const config_t *config, control_t *control)
{
    settings_t *self = g_malloc0(sizeof *self);
    settings_ctor(self, config, control);
    return self;
}

void
settings_delete(settings_t *self)
{
    if( self ) {
        settings_dtor(self);
        g_free(self);
    }
}

void
settings_delete_at(settings_t **pself)
{
    settings_delete(*pself), *pself = NULL;
}

void
settings_delete_cb(void *self)
{
    settings_delete(self);
}

/* ------------------------------------------------------------------------- *
 * SETTINGS_ATTRIBUTES
 * ------------------------------------------------------------------------- */

#ifdef DEAD_CODE
static const config_t *
settings_config(const settings_t *self)
{
    return self->stt_config;
}
#endif

static control_t *
settings_control(const settings_t *self)
{
    return self->stt_control;
}

appsettings_t *
settings_appsettings(settings_t *self, uid_t uid, const char *app)
{
    appsettings_t *appsettings = NULL;
    control_t *control = settings_control(self);
    if( control_valid_user(control, uid) &&
        control_valid_application(control, app) )
        appsettings = settings_add_appsettings(self, uid, app);
    return appsettings;
}

static bool
settings_initialized(const settings_t *self)
{
    return self->stt_initialized;
}

/* ------------------------------------------------------------------------- *
 * SETTINGS_USERSETTINGS
 * ------------------------------------------------------------------------- */

usersettings_t *
settings_get_usersettings(const settings_t *self, uid_t uid)
{
    return g_hash_table_lookup(self->stt_users, GINT_TO_POINTER(uid));
}

usersettings_t *
settings_add_usersettings(settings_t *self, uid_t uid)
{
    usersettings_t *usersettings = settings_get_usersettings(self, uid);
    if( !usersettings ) {
        usersettings = usersettings_create(self, uid);
        g_hash_table_insert(self->stt_users, GINT_TO_POINTER(uid),
                            usersettings);
    }
    return usersettings;
}

bool
settings_remove_usersettings(settings_t *self, uid_t uid)
{
    return g_hash_table_remove(self->stt_users, GINT_TO_POINTER(uid));
}

/* ------------------------------------------------------------------------- *
 * SETTINGS_APPSETTING
 * ------------------------------------------------------------------------- */

appsettings_t *
settings_get_appsettings(const settings_t *self, uid_t uid, const char *appname)
{
    appsettings_t *appsettings = NULL;
    usersettings_t *usersettings = settings_get_usersettings(self, uid);
    if( usersettings )
        appsettings = usersettings_get_appsettings(usersettings, appname);
    return appsettings;
}

appsettings_t *
settings_add_appsettings(settings_t *self, uid_t uid, const char *appname)
{
    usersettings_t *usersettings = settings_add_usersettings(self, uid);
    return usersettings_add_appsettings(usersettings, appname);
}

bool
settings_remove_appsettings(settings_t *self, uid_t uid, const char *appname)
{
    bool removed = false;
    usersettings_t *usersettings = settings_get_usersettings(self, uid);
    if( usersettings )
        removed = usersettings_remove_appsettings(usersettings, appname);
    return removed;
}

/* ------------------------------------------------------------------------- *
 * SETTINGS_STORAGE
 * ------------------------------------------------------------------------- */

void
settings_load_all(settings_t *self)
{
    control_t *control = settings_control(self);
    uid_t min_uid = control_min_user(control);
    uid_t max_uid = control_max_user(control);

    for( uid_t uid = min_uid; uid <= max_uid; ++uid )
        settings_load_user(self, uid);
}

void
settings_save_all(const settings_t *self)
{
    control_t *control = settings_control(self);
    uid_t min_uid = control_min_user(control);
    uid_t max_uid = control_max_user(control);

    for( uid_t uid = min_uid; uid <= max_uid; ++uid )
        settings_save_user(self, uid);
}

void
settings_load_user(settings_t *self, uid_t uid)
{
    if( control_valid_user(settings_control(self), uid) ) {
        gchar *path = settings_userdata_path(uid);
        usersettings_t *usersettings = settings_add_usersettings(self, uid);
        usersettings_load(usersettings, path);
        g_free(path);
    }
}

void
settings_save_user(const settings_t *self, uid_t uid)
{

    if( control_valid_user(settings_control(self), uid) ) {
        gchar *path = settings_userdata_path(uid);
        usersettings_t *usersettings = settings_get_usersettings(self, uid);
        if( usersettings )
            usersettings_save(usersettings, path);
        g_free(path);
    }
}

static void
settings_save_now(settings_t *self)
{
    settings_cancel_save(self);

    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, self->stt_user_changes);
    while( g_hash_table_iter_next(&iter, &key, &value) ) {
        uid_t uid = GPOINTER_TO_UINT(key);
        settings_save_user(self, uid);
    }

    g_hash_table_remove_all(self->stt_user_changes);
}

static gboolean
settings_save_cb(gpointer aptr)
{
    settings_t *self = aptr;
    self->stt_save_id = 0;
    settings_save_now(self);
    return G_SOURCE_REMOVE;
}

static void
settings_cancel_save(settings_t *self)
{
    if( self->stt_save_id ) {
        g_source_remove(self->stt_save_id),
            self->stt_save_id = 0;
    }
}

void
settings_save_later(settings_t *self, uid_t uid)
{
    g_hash_table_add(self->stt_user_changes, GINT_TO_POINTER(uid));

    if( !self->stt_save_id ) {
        self->stt_save_id = g_timeout_add(1000, settings_save_cb, self);
    }
}

/* ------------------------------------------------------------------------- *
 * SETTINGS_RETHINK
 * ------------------------------------------------------------------------- */

void
settings_rethink(settings_t *self)
{
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, self->stt_users);
    while( g_hash_table_iter_next(&iter, &key, &value) )
        usersettings_rethink(value);
}

/* ------------------------------------------------------------------------- *
 * SETTINGS_UTILITY
 * ------------------------------------------------------------------------- */

static gchar *
settings_userdata_path(uid_t uid)
{
    return g_strdup_printf(SETTINGS_DIRECTORY "/user-%u" SETTINGS_EXTENSION,
                           (unsigned)uid);
}

/* ========================================================================= *
 * USERSETTINGS
 * ========================================================================= */

struct usersettings_t
{
    settings_t *ust_settings;
    uid_t       ust_uid;
    GHashTable *ust_apps;
};

static void
usersettings_ctor(usersettings_t *self, settings_t *settings, uid_t uid)
{
    self->ust_settings = settings;
    self->ust_uid      = uid;
    self->ust_apps     = g_hash_table_new_full(g_str_hash,
                                               g_str_equal,
                                               g_free,
                                               appsettings_delete_cb);
    log_info("usersettings(%d) created", (int)usersettings_uid(self));
}

static void
usersettings_dtor(usersettings_t *self)
{
    log_info("usersettings(%d) deleted", (int)usersettings_uid(self));
    if( self->ust_apps ) {
        g_hash_table_unref(self->ust_apps),
            self->ust_apps = NULL;
    }
}

usersettings_t *
usersettings_create(settings_t *settings, uid_t uid)
{
    usersettings_t *self = g_malloc0(sizeof *self);
    usersettings_ctor(self, settings, uid);
    return self;
}

void
usersettings_delete(usersettings_t *self)
{
    if( self ) {
        usersettings_dtor(self);
        g_free(self);
    }
}

void
usersettings_delete_cb(void *self)
{
    usersettings_delete(self);
}

/* ------------------------------------------------------------------------- *
 * USERSETTINGS_ATTRIBUTES
 * ------------------------------------------------------------------------- */

static settings_t *
usersettings_settings(const usersettings_t *self)
{
    return self->ust_settings;
}

static uid_t
usersettings_uid(const usersettings_t *self)
{
    return self->ust_uid;
}

#ifdef DEAD_CODE
static const config_t *
usersettings_config(const usersettings_t *self)
{
    return settings_config(usersettings_settings(self));
}
#endif

static control_t *
usersettings_control(const usersettings_t *self)
{
    return settings_control(usersettings_settings(self));
}

/* ------------------------------------------------------------------------- *
 * USERSETTINGS_APPSETTINGS
 * ------------------------------------------------------------------------- */

appsettings_t *
usersettings_get_appsettings(const usersettings_t *self, const gchar *appname)
{
    return g_hash_table_lookup(self->ust_apps, appname);
}

appsettings_t *
usersettings_add_appsettings(usersettings_t *self, const gchar *appname)
{
    appsettings_t *appsettings = usersettings_get_appsettings(self, appname);
    if( !appsettings ) {
        appsettings = appsettings_create(self, appname);
        g_hash_table_insert(self->ust_apps, g_strdup(appname), appsettings);
    }
    return appsettings;
}

bool
usersettings_remove_appsettings(usersettings_t *self, const gchar *appname)
{
    return g_hash_table_remove(self->ust_apps, appname);
}

/* ------------------------------------------------------------------------- *
 * USERSETTINGS_STORAGE
 * ------------------------------------------------------------------------- */

void
usersettings_load(usersettings_t *self, const char *path)
{
    GKeyFile *file = g_key_file_new();
    keyfile_load(file, path);
    gchar **groups = g_key_file_get_groups(file, NULL);
    if( groups ) {
        for( size_t i = 0; groups[i]; ++i ) {
            const char *appname = groups[i];
            if( control_valid_application(usersettings_control(self), appname) ) {
                appsettings_t *appsettings =
                    usersettings_add_appsettings(self, appname);
                appsettings_decode(appsettings, file);
            }
        }
        g_strfreev(groups);
    }
    g_key_file_unref(file);
}

void
usersettings_save(const usersettings_t *self, const char *path)
{
    GKeyFile *file = g_key_file_new();
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, self->ust_apps);
    while( g_hash_table_iter_next(&iter, &key, &value) ) {
        const char *appname = key;
        if( control_valid_application(usersettings_control(self), appname) ) {
            appsettings_t *appsettings = value;
            appsettings_encode(appsettings, file);
        }
    }
    keyfile_save(file, path);
    g_key_file_unref(file);
}

/* ------------------------------------------------------------------------- *
 * USERSETTINGS_RETHINK
 * ------------------------------------------------------------------------- */

static void
usersettings_rethink(usersettings_t *self)
{
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, self->ust_apps);
    while( g_hash_table_iter_next(&iter, &key, &value) )
        appsettings_rethink(value);
}

/* ========================================================================= *
 * APPSETTINGS
 * ========================================================================= */

struct appsettings_t
{
    usersettings_t *ast_usersettings;
    gchar          *ast_appname;

    app_allowed_t   ast_allowed;
    app_agreed_t    ast_agreed;
    stringset_t    *ast_granted;
};

static void
appsettings_ctor(appsettings_t *self, usersettings_t *usersettings,
                 const char *appname)
{
    self->ast_usersettings = usersettings;
    self->ast_appname      = g_strdup(appname);

    self->ast_allowed      = APP_ALLOWED_UNSET;
    self->ast_agreed       = APP_AGREED_UNSET;
    self->ast_granted      = stringset_create();

    log_info("appsettings(%d, %s) created",
             (int)usersettings_uid(appsettings_usersettings(self)),
             appsettings_appname(self));
}

static void
appsettings_dtor(appsettings_t *self)
{
    log_info("appsettings(%d, %s) deleted",
             (int)usersettings_uid(appsettings_usersettings(self)),
             appsettings_appname(self));

    change_string(&self->ast_appname, NULL);
    stringset_delete_at(&self->ast_granted);
}

appsettings_t *
appsettings_create(usersettings_t *usersettings, const char *appname)
{
    appsettings_t *self = g_malloc0(sizeof *self);
    appsettings_ctor(self, usersettings, appname);
    return self;
}

void
appsettings_delete(appsettings_t *self)
{
    if( self ) {
        appsettings_dtor(self);
        g_free(self);
    }
}

void
appsettings_delete_cb(void *self)
{
    appsettings_delete(self);
}

/* ------------------------------------------------------------------------- *
 * APPSETTINGS_ATTRIBUTES
 * ------------------------------------------------------------------------- */

#ifdef DEAD_CODE
static const config_t *
appsettings_config(const appsettings_t *self)
{
    return usersettings_config(appsettings_usersettings(self));
}
#endif

static control_t *
appsettings_control(const appsettings_t *self)
{
    return settings_control(appsettings_settings(self));
}

static settings_t *
appsettings_settings(const appsettings_t *self)
{
    return usersettings_settings(appsettings_usersettings(self));
}

static usersettings_t *
appsettings_usersettings(const appsettings_t *self)
{
    return self->ast_usersettings;
}

static uid_t
appsettings_uid(appsettings_t *self)
{
    return usersettings_uid(appsettings_usersettings(self));
}

static const gchar *
appsettings_appname(const appsettings_t *self)
{
    return self->ast_appname;
}

/* ------------------------------------------------------------------------- *
 * APPSETTINGS_NOTIFY
 * ------------------------------------------------------------------------- */

static void
appsettings_notify_change(appsettings_t *self)
{
    /* Forward application changes upwards */
    if( settings_initialized(appsettings_settings(self)) )
        control_on_settings_change(appsettings_control(self),
                                   appsettings_appname(self));

    /* Schedule user settings saving */
    settings_save_later(appsettings_settings(self),
                        appsettings_uid(self));
}

/* ------------------------------------------------------------------------- *
 * APPSETTINGS_PROPERTIES
 * ------------------------------------------------------------------------- */

app_allowed_t
appsettings_get_allowed(const appsettings_t *self)
{
    return self->ast_allowed;
}

app_agreed_t
appsettings_get_agreed(const appsettings_t *self)
{
    return self->ast_agreed;
}

const stringset_t *
appsettings_get_granted(appsettings_t *self)
{
    return self->ast_granted;
}

void
appsettings_set_allowed(appsettings_t *self, app_allowed_t allowed)
{
    if( (unsigned)allowed >= APP_ALLOWED_COUNT )
        allowed = APP_ALLOWED_UNSET;

    if( self->ast_allowed != allowed ) {
        self->ast_allowed = allowed;
        log_debug("[%u] %s: allowed = %s",
                  appsettings_uid(self),
                  appsettings_appname(self),
                  app_allowed_name[allowed]);
        appsettings_notify_change(self);

        const stringset_t *granted = NULL;
        if( allowed == APP_ALLOWED_ALWAYS ) {
            appinfo_t *appinfo = control_appinfo(appsettings_control(self),
                                                 appsettings_appname(self));
            if( appinfo )
                granted = appinfo_get_permissions(appinfo);
        }
        appsettings_set_granted(self, granted);
    }
}

void
appsettings_set_agreed(appsettings_t *self, app_agreed_t agreed)
{
    if( (unsigned)agreed >= APP_AGREED_COUNT  )
        agreed = APP_AGREED_UNSET;

    if( self->ast_agreed != agreed ) {
        self->ast_agreed = agreed;
        log_debug("[%u] %s: agreed = %s",
                  appsettings_uid(self),
                  appsettings_appname(self),
                  app_agreed_name[agreed]);
        appsettings_notify_change(self);
    }
}

void
appsettings_set_granted(appsettings_t *self, const stringset_t *granted)
{
    /* Note: This must be kept so that it works also when
     *       'granted' arg is actually the current value,
     *       so that it can be used also for re-evaluating
     *       state after desktop file changes.
     */

    stringset_t *dummy = NULL;
    stringset_t *masked = NULL;

    if( appsettings_get_allowed(self) != APP_ALLOWED_ALWAYS )
        granted = NULL;

    /* Allow use of granted=NULL to clear */
    if( !granted )
        granted = dummy = stringset_create();

    appinfo_t *appinfo = control_appinfo(appsettings_control(self),
                                         appsettings_appname(self));

    if( !appinfo_valid(appinfo) ) {
        // APP.desktop data not available -> use empty permssion set
        masked = stringset_create();
    }
    else {
        const stringset_t *mask = appinfo_get_permissions(appinfo);
        masked = stringset_filter_in(granted, mask);
    }

    if( stringset_assign(self->ast_granted, masked) ) {
        gchar *text = stringset_to_string(self->ast_granted);
        log_debug("[%u] %s: granted = %s",
                  appsettings_uid(self),
                  appsettings_appname(self),
                  text);
        g_free(text);
        appsettings_notify_change(self);
    }

    stringset_delete(masked);
    stringset_delete(dummy);
}

/* ------------------------------------------------------------------------- *
 * APPSETTINGS_STORAGE
 * ------------------------------------------------------------------------- */

static void
appsettings_decode(appsettings_t *self, GKeyFile *file)
{
    const char *sec = appsettings_appname(self);
    self->ast_allowed = keyfile_get_integer(file, sec, "Allowed",
                                            APP_ALLOWED_UNSET);
    self->ast_agreed  = keyfile_get_integer(file, sec, "Agreed",
                                            APP_AGREED_UNSET);

    /* 'Granted' needs to be subjected to permissions available in
     * system, permissions requested in desktop file and current
     * state of 'allowed' setting -> it needs to be pushed through
     * evaluator rather than being used as-is.
     */
    stringset_t *granted = keyfile_get_stringset(file, sec, "Granted");
    appsettings_set_granted(self, granted);
    stringset_delete(granted);
}

static void
appsettings_encode(const appsettings_t *self, GKeyFile *file)
{
    const char *sec = appsettings_appname(self);
    keyfile_set_integer(file, sec, "Allowed", self->ast_allowed);
    keyfile_set_integer(file, sec, "Agreed", self->ast_agreed);
    keyfile_set_stringset(file, sec, "Granted", self->ast_granted);
}

/* ------------------------------------------------------------------------- *
 * APPSETTINGS_RETHINK
 * ------------------------------------------------------------------------- */

static void
appsettings_rethink(appsettings_t *self)
{
    /* Assign current value -> masking is re-applied */
    appsettings_set_granted(self, self->ast_granted);
}
