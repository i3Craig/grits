/*
 * Copyright (C) 2009-2011 Andy Spencer <andy753421@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:grits-prefs
 * @short_description: Persistent preference handing
 *
 * #GritsPrefs is used to store and access preferences in grits. It is mostly a
 * wrapper around a #GKeyFile. Preferences can be stored for the application
 * using grits, but may also be stored by grits itself. An example of this are
 * whether grits is in online or offline mode. Many #GritsPlugin<!-- -->s also
 * store preferences.
 *
 * There are two variants of preference functions. The normal variant takes
 * group and a key separated by a "/" as they key to the preference. The "_v"
 * variant takes the group and the key as separate parameters.
 */

#include <config.h>

#include <glib.h>
#include <time.h>
#include "grits-marshal.h"
#include "grits-prefs.h"

enum {
	SIG_PREF_CHANGED,
	NUM_SIGNALS,
};
static guint signals[NUM_SIGNALS];

/* Helper functions */
static void grits_prefs_save(GritsPrefs *prefs)
{
	g_debug("GritsPrefs: save");
	gsize length;
	gchar *dir = g_path_get_dirname(prefs->key_path);
	if (!g_file_test(dir, G_FILE_TEST_EXISTS))
		g_mkdir_with_parents(dir, 0755);
	gchar *data = g_key_file_to_data(prefs->key_file, &length, NULL);
	g_file_set_contents(prefs->key_path, data, length, NULL);
	g_free(dir);
	g_free(data);
}
static gboolean grits_prefs_try_save(GritsPrefs *prefs)
{
	const  time_t interval = 1;
	static time_t lastsave = 0;
	static guint  source   = 0;
	if (time(NULL) - lastsave > interval) {
		grits_prefs_save(prefs);
		lastsave = time(NULL);
		source   = 0;
	} else if (source == 0) {
		source = g_timeout_add_seconds(interval,
			(GSourceFunc)grits_prefs_try_save, prefs);
	}
	return FALSE;
}

/***********
 * Methods *
 ***********/
/**
 * grits_prefs_new:
 * @config:   the path to the config file
 * @defaults: the path to the default config file
 *
 * Create a new preference object for the given @config. If the config does not
 * exist the @defaults file is loaded.
 *
 * Returns: the new #GritsPrefs
 */
GritsPrefs *grits_prefs_new(const gchar *config, const gchar *defaults)
{
	g_debug("GritsPrefs: new - %s, %s", config, defaults);
	GritsPrefs *prefs = g_object_new(GRITS_TYPE_PREFS, NULL);
	if (config)
		prefs->key_path = g_strdup(config);
	else
		prefs->key_path = g_build_filename(g_get_user_config_dir(),
				PACKAGE, "config.ini", NULL);
	GError *error = NULL;
	g_key_file_load_from_file(prefs->key_file, prefs->key_path,
			G_KEY_FILE_KEEP_COMMENTS, &error);
	if (error && defaults) {
		g_debug("GritsPrefs: new - Trying defaults");
		g_clear_error(&error);
		g_key_file_load_from_file(prefs->key_file, defaults,
				G_KEY_FILE_KEEP_COMMENTS, &error);
	}
	if (error) {
		g_debug("GritsPrefs: new - Trying GRITS defaults");
		g_clear_error(&error);
		gchar *tmp = g_build_filename(PKGDATADIR, "defaults.ini", NULL);
		g_key_file_load_from_file(prefs->key_file, tmp,
				G_KEY_FILE_KEEP_COMMENTS, &error);
		g_free(tmp);
	}
	if (error) {
		g_debug("GritsPrefs: new - Unable to load key file `%s': %s",
			prefs->key_path, error->message);
	}
	g_debug("GritsPrefs: new - using %s", prefs->key_path);
	return prefs;
}

#define make_pref_type(name, c_type, g_type)                                         \
c_type grits_prefs_get_##name##_v(GritsPrefs *prefs,                                 \
		const gchar *group, const gchar *key, GError **_error)               \
{                                                                                    \
	GError *error = NULL;                                                        \
	c_type value = g_key_file_get_##name(prefs->key_file, group, key, &error);   \
	if (error && error->code != G_KEY_FILE_ERROR_GROUP_NOT_FOUND &&              \
	             error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)                  \
		g_warning("GritsPrefs: get_"#name" - error getting key %s: %s\n",    \
				key, error->message);                                \
	if (error && _error)                                                         \
		*_error = error;                                                     \
	return value;                                                                \
}                                                                                    \
c_type grits_prefs_get_##name(GritsPrefs *prefs, const gchar *key, GError **error)   \
{                                                                                    \
	gchar **keys  = g_strsplit(key, "/", 2);                                     \
	c_type value = grits_prefs_get_##name##_v(prefs, keys[0], keys[1], error);   \
	g_strfreev(keys);                                                            \
	return value;                                                                \
}                                                                                    \
                                                                                     \
void grits_prefs_set_##name##_v(GritsPrefs *prefs,                                   \
		const gchar *group, const gchar *key, const c_type value)            \
{                                                                                    \
	g_key_file_set_##name(prefs->key_file, group, key, value);                   \
	gchar *all = g_strconcat(group, "/", key, NULL);                             \
	g_signal_emit(prefs, signals[SIG_PREF_CHANGED], 0,                           \
			all, g_type, &value);                                        \
	grits_prefs_try_save(prefs);                                                 \
	g_free(all);                                                                 \
}                                                                                    \
void grits_prefs_set_##name(GritsPrefs *prefs, const gchar *key, const c_type value) \
{                                                                                    \
	gchar **keys = g_strsplit(key, "/", 2);                                      \
	grits_prefs_set_##name##_v(prefs, keys[0], keys[1], value);                  \
	g_strfreev(keys);                                                            \
}                                                                                    \

make_pref_type(string,  gchar*,   G_TYPE_STRING)
make_pref_type(boolean, gboolean, G_TYPE_BOOLEAN)
make_pref_type(integer, gint,     G_TYPE_INT)
make_pref_type(double,  gdouble,  G_TYPE_DOUBLE)


/****************
 * GObject code *
 ****************/
G_DEFINE_TYPE(GritsPrefs, grits_prefs, G_TYPE_OBJECT);
static void grits_prefs_init(GritsPrefs *prefs)
{
	g_debug("GritsPrefs: init");
	prefs->key_file = g_key_file_new();
}
static void grits_prefs_dispose(GObject *_prefs)
{
	g_debug("GritsPrefs: dispose");
	GritsPrefs *prefs = GRITS_PREFS(_prefs);
	if (prefs->key_file) {
		grits_prefs_save(prefs);
		g_key_file_free(prefs->key_file);
		g_free(prefs->key_path);
		prefs->key_file = NULL;
	}
	G_OBJECT_CLASS(grits_prefs_parent_class)->dispose(_prefs);
}
static void grits_prefs_class_init(GritsPrefsClass *klass)
{
	g_debug("GritsPrefs: class_init");
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->dispose = grits_prefs_dispose;

	/**
	 * GritsPrefs::pref-changed:
	 * @prefs: the preference store.
	 * @key:   the key to the preference.
	 * @type:  the type of the preference that changed.
	 * @value: a pointer to the value of the preference.
	 *
	 * The ::pref-changed signal is emitted each time a preference is
	 * changed.
	 */
	signals[SIG_PREF_CHANGED] = g_signal_new(
			"pref-changed",
			G_TYPE_FROM_CLASS(gobject_class),
			G_SIGNAL_RUN_LAST,
			0,
			NULL,
			NULL,
			grits_cclosure_marshal_VOID__STRING_UINT_POINTER,
			G_TYPE_NONE,
			3,
			G_TYPE_STRING,
			G_TYPE_UINT,
			G_TYPE_POINTER);
}
