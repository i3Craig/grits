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
 * SECTION:env
 * @short_description: Environment plugin
 *
 * #GritsPluginEnv provides environmental information such as sky images. It can
 * also paint a blank overlay on the surface so that other plugins can draw
 * transparent overlays nicely.
 */

#include <math.h>

#include <grits.h>

#include "env.h"

/***********
 * Helpers *
 ***********/

/* Sky */
static void sky_expose(GritsCallback *sky, GritsOpenGL *opengl, gpointer _env)
{
	GritsPluginEnv *env = GRITS_PLUGIN_ENV(_env);
	g_debug("GritsPluginEnv: expose_sky");

	gdouble lat, lon, elev;
	grits_viewer_get_location(env->viewer, &lat, &lon, &elev);

	/* Misc */
	gdouble rg   = MAX(0, 1-(elev/40000));
	gdouble blue = MAX(0, 1-(elev/100000));
	glClearColor(MIN(0.4,rg), MIN(0.4,rg), MIN(1,blue), 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	/* Attempt to render an atmosphere */
	glEnable(GL_COLOR_MATERIAL);
	glDisable(GL_CULL_FACE);
	glDisable(GL_LIGHTING);
	glMatrixMode(GL_MODELVIEW);
	glBlendFunc(GL_SRC_ALPHA, GL_DST_ALPHA);
	grits_viewer_center_position(env->viewer, lat, lon, -EARTH_R);

	gdouble ds  = EARTH_R+elev;     // distance to self
	gdouble da  = EARTH_R+300000;   // distance to top of atmosphere
	gdouble dg  = EARTH_R-100000;   // distance to top of atmosphere
	gdouble ang = acos(EARTH_R/ds); // angle to horizon
	ang = MAX(ang,0.1);

	gdouble ar  = sin(ang)*da;      // top of quad fan "atomosphere"j
	gdouble az  = cos(ang)*da;      //

	gdouble gr  = sin(ang)*dg;      // bottom of quad fan "ground"
	gdouble gz  = cos(ang)*dg;      //

	glBegin(GL_QUAD_STRIP);
	for (gdouble i = 0; i <= 2*G_PI; i += G_PI/30) {
		glColor4f(0.3, 0.3, 1.0, 1.0); glVertex3f(gr*sin(i), gr*cos(i), gz);
		glColor4f(0.3, 0.3, 1.0, 0.0); glVertex3f(ar*sin(i), ar*cos(i), az);
	}
	glEnd();
}

/* Compass */
static void compass_draw_compass(gdouble scale)
{
	gfloat thick = scale * 0.20;

	/* Setup lighting */
	float light_ambient[]  = {0.4f, 0.4f, 0.4f, 0.4f};
	float light_diffuse[]  = {0.9f, 0.9f, 0.9f, 1.0f};
	float light_position[] = {-scale*2, -scale*4, -scale*0.5, 1.0f};
	glLightfv(GL_LIGHT0, GL_AMBIENT,  light_ambient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE,  light_diffuse);
	glLightfv(GL_LIGHT0, GL_POSITION, light_position);

	/* Compass data */
	gdouble colors[][3] = {
		{1, 0, 0},
		{1, 1, 1},
		{1, 1, 1},
		{1, 1, 1},
	};
	gdouble points[][3] = {
		{     0, -scale,      0},
		{     0,      0,  thick},
		{ thick, -thick,      0},
		{     0,      0, -thick},
		{-thick, -thick,      0},
	};
	gint faces[][3] = {
		{1, 0, 2},
		{2, 0, 3},
		{3, 0, 4},
		{4, 0, 1},
	};
	gint outline[] = {
		2, 0, 4,
		1, 2, 3, 4,
	};

	/* Draw compas */
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1, 1);
	for (int i = 0; i < G_N_ELEMENTS(colors); i++) {
		gdouble *color = colors[i];
		glColor3dv(color);
		for (int j = 0; j < G_N_ELEMENTS(faces); j++) {
			gdouble norm[3];
			gdouble *v0 = points[faces[j][0]];
			gdouble *v1 = points[faces[j][1]];
			gdouble *v2 = points[faces[j][2]];
			crossd3(v2, v1, v0, norm);
			glNormal3dv(norm);
			glBegin(GL_TRIANGLES);
			glVertex3dv(v0);
			glVertex3dv(v1);
			glVertex3dv(v2);
			glEnd();
		}
		glRotatef(90, 0, 0, 1);
	}

	/* Draw outline */
	glEnable(GL_POLYGON_OFFSET_LINE);
	glDisable(GL_LIGHTING);
	glPolygonOffset(0, 0);
	glColor4f(0, 0, 0, 0.25);
	glLineWidth(1);
	for (int i = 0; i < G_N_ELEMENTS(colors); i++) {
		glBegin(GL_LINE_STRIP);
		for (int j = 0; j < G_N_ELEMENTS(outline); j++)
			glVertex3dv(points[outline[j]]);
		glEnd();
		glRotatef(90, 0, 0, 1);
	}
}

static void compass_expose(GritsCallback *compass, GritsOpenGL *opengl, gpointer _env)
{
	GritsPluginEnv *env = GRITS_PLUGIN_ENV(_env);
	g_debug("GritsPluginEnv: compass_expose");
	gdouble x, y, z;
	grits_viewer_get_rotation(env->viewer, &x, &y, &z);

	/* Setup projection */
	GtkAllocation alloc;
	gtk_widget_get_allocation(GTK_WIDGET(opengl), &alloc);
	float scale     = CLAMP(MIN(alloc.width,alloc.height)/2.0 * 0.1, 40, 100);
	float offset    = scale + 20;
	glTranslatef(alloc.width - offset, offset, 0);

	/* Setup state */
	glClear(GL_DEPTH_BUFFER_BIT);
	glEnable(GL_COLOR_MATERIAL);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_POLYGON_SMOOTH);
	glEnable(GL_LINE_SMOOTH);

	/* Draw compass */
	x = CLAMP(x, -66, 66);;
	glRotatef(x, 1, 0, 0);
	glRotatef(-z, 0, 0, 1);
	compass_draw_compass(scale);
}

static gboolean compass_click(GritsCallback *compass, GdkEvent *evnet, GritsViewer *viewer)
{
	grits_viewer_set_rotation(viewer, 0, 0, 0);
	return TRUE;
}

/* Info */
static void info_expose(GritsCallback *compass, GritsOpenGL *opengl, gpointer _env)
{
	GtkAllocation alloc;
	gtk_widget_get_allocation(GTK_WIDGET(opengl), &alloc);

	/* Create cairo  surface */
	guint            tex     = 0;
	const gchar     *label0  = "Location: %7.3lf°, %8.3lf°, %4.0fm";
	const gchar     *label1  = "Cursor:   %7.3lf°, %8.3lf°, %4.0fm";
	gdouble          width   = 300;
	gdouble          height  = 200;
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	cairo_t         *cairo   = cairo_create(surface);

	/* Text */
	gdouble lat, lon, elev;
	grits_viewer_get_location(GRITS_VIEWER(opengl), &lat, &lon, &elev);
	gchar *text0 = g_strdup_printf(label0, lat, lon, elev);
	gchar *text1 = g_strdup_printf(label1, lat, lon, elev);

	/* Draw outline */
	cairo_set_line_width(cairo, 3);
	cairo_set_source_rgba(cairo, 0, 0, 0, 0.75);
	cairo_move_to(cairo, 2, 20); cairo_text_path(cairo, text0);
	cairo_move_to(cairo, 2, 40); cairo_text_path(cairo, text1);
	cairo_stroke(cairo);

	/* Draw filler */
	cairo_set_source_rgba(cairo, 1, 1, 1, 1);
	cairo_move_to(cairo, 2, 20); cairo_show_text(cairo, text0);
	cairo_move_to(cairo, 2, 40); cairo_show_text(cairo, text1);

	/* Setup pango */
	PangoLayout          *layout = pango_cairo_create_layout(cairo);
	PangoFontDescription *font   = pango_font_description_from_string("Mono 9");
	pango_layout_set_font_description(layout, font);
	pango_font_description_free(font);
	pango_layout_set_text(layout, text0, -1);
	pango_cairo_update_layout(cairo, layout);
	cairo_set_line_join(cairo, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to(cairo, 2, 40);
	pango_cairo_layout_path(cairo, layout);
	for (float w = 0.2; w <= 0.8; w+=0.2) {
		cairo_set_line_width(cairo, (1-w)*8);
		cairo_set_source_rgba(cairo, 0, 0, 0, w);
		cairo_stroke_preserve(cairo);
	}
	cairo_set_source_rgba(cairo, 1, 1, 1, 1);
	pango_cairo_show_layout(cairo, layout);

	/* Load GL texture */
	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, 4, width, height,
	        0, GL_BGRA, GL_UNSIGNED_BYTE, cairo_image_surface_get_data(surface));
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	/* Draw surface */
	glDisable(GL_LIGHTING);
	glDisable(GL_COLOR_MATERIAL);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex);
	glDisable(GL_CULL_FACE);
	glTranslatef(alloc.width - width, alloc.height - height, 0);
	glBegin(GL_QUADS);
	glTexCoord2f(1, 0); glVertex3f(width, 0     , 0); // 0 - 3    0
	glTexCoord2f(1, 1); glVertex3f(width, height, 0); // 1 - |    |
	glTexCoord2f(0, 1); glVertex3f(0    , height, 0); // 2 - |    |
	glTexCoord2f(0, 0); glVertex3f(0    , 0     , 0); // 3 - 2----1
	glEnd();
}

/***********
 * Methods *
 ***********/
/**
 * grits_plugin_env_new:
 * @viewer: the #GritsViewer to use for drawing
 * @prefs:  the #GritsPrefs for storing configurations
 *
 * Create a new instance of the environment plugin.
 *
 * Returns: the new #GritsPluginEnv
 */
GritsPluginEnv *grits_plugin_env_new(GritsViewer *viewer, GritsPrefs *prefs)
{
	g_debug("GritsPluginEnv: new");
	GritsPluginEnv *env = g_object_new(GRITS_TYPE_PLUGIN_ENV, NULL);
	env->viewer = g_object_ref(viewer);
	env->prefs  = g_object_ref(prefs);

	/* Add sky */
	GritsCallback *sky = grits_callback_new(sky_expose, env);
	grits_viewer_add(viewer, GRITS_OBJECT(sky), GRITS_LEVEL_BACKGROUND, FALSE);
	env->refs = g_list_prepend(env->refs, sky);

	/* Add compass */
	GritsCallback *compass = grits_callback_new(compass_expose, env);
	grits_viewer_add(viewer, GRITS_OBJECT(compass), GRITS_LEVEL_HUD, TRUE);
	g_signal_connect(compass, "clicked", G_CALLBACK(compass_click), viewer);
	grits_object_set_cursor(GRITS_OBJECT(compass), GDK_CROSS);
	env->refs = g_list_prepend(env->refs, compass);

	/* Add info */
	//GritsCallback *info = grits_callback_new(info_expose, env);
	//grits_viewer_add(viewer, GRITS_OBJECT(info), GRITS_LEVEL_HUD, FALSE);
	//env->refs = g_list_prepend(env->refs, info);
	(void)info_expose;

	/* Add background */
	//GritsTile *background = grits_tile_new(NULL, NORTH, SOUTH, EAST, WEST);
	//glGenTextures(1, &env->tex);
	//background->data = &env->tex;
	//grits_viewer_add(viewer, GRITS_OBJECT(background), GRITS_LEVEL_BACKGROUND, FALSE);
	//env->refs = g_list_prepend(env->refs, background);

	return env;
}


/****************
 * GObject code *
 ****************/
/* Plugin init */
static void grits_plugin_env_plugin_init(GritsPluginInterface *iface);
G_DEFINE_TYPE_WITH_CODE(GritsPluginEnv, grits_plugin_env, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(GRITS_TYPE_PLUGIN,
			grits_plugin_env_plugin_init));
static void grits_plugin_env_plugin_init(GritsPluginInterface *iface)
{
	g_debug("GritsPluginEnv: plugin_init");
	/* Add methods to the interface */
}
/* Class/Object init */
static void grits_plugin_env_init(GritsPluginEnv *env)
{
	g_debug("GritsPluginEnv: init");
	/* Set defaults */
}
static void grits_plugin_env_dispose(GObject *gobject)
{
	g_debug("GritsPluginEnv: dispose");
	GritsPluginEnv *env = GRITS_PLUGIN_ENV(gobject);
	/* Drop references */
	if (env->viewer) {
		for (GList *cur = env->refs; cur; cur = cur->next)
			grits_object_destroy_pointer(&cur->data);
		g_object_unref(env->viewer);
		g_object_unref(env->prefs);
		glDeleteTextures(1, &env->tex);
		env->viewer = NULL;
	}
	G_OBJECT_CLASS(grits_plugin_env_parent_class)->dispose(gobject);
}
static void grits_plugin_env_class_init(GritsPluginEnvClass *klass)
{
	g_debug("GritsPluginEnv: class_init");
	GObjectClass *gobject_class = (GObjectClass*)klass;
	gobject_class->dispose = grits_plugin_env_dispose;
}
