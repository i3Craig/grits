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
 * SECTION:grits-opengl
 * @short_description: OpenGL based virtual globe
 *
 * #GritsOpenGL is the core rendering engine used by grits. Theoretically other
 * renderers could be writte, but they have not been. GritsOpenGL uses the ROAM
 * algorithm for updating surface mesh the planet. The only thing GritsOpenGL
 * can actually render on it's own is a wireframe of a sphere.
 *
 * GritsOpenGL requires (at least) OpenGL 2.0.
 */

#include <config.h>
#include <math.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "grits-opengl.h"
#include "grits-util.h"
#include "gtkgl.h"
#include "roam.h"

// #define ROAM_DEBUG

#define OVERLAY_SLICE 0.01

/* Tessellation, "finding intersecting triangles" */
/* http://research.microsoft.com/pubs/70307/tr-2006-81.pdf */
/* http://www.opengl.org/wiki/Alpha_Blending */

/* The unsorted/sroted GLists are blank head nodes,
 * This way us we can remove objects from the level just by fixing up links
 * I.e. we don't need to do a lookup to remove an object if we have its GList */
struct RenderLevel {
	gint  num;
	GList unsorted;
	GList sorted;
};

/***********
 * Helpers *
 ***********/
static void _set_projection(GritsOpenGL *opengl)
{
	double lat, lon, elev, rx, ry, rz;
	grits_viewer_get_location(GRITS_VIEWER(opengl), &lat, &lon, &elev);
	grits_viewer_get_rotation(GRITS_VIEWER(opengl), &rx, &ry, &rz);

	/* Set projection and clipping planes */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	GtkAllocation alloc;
	gtk_widget_get_allocation(GTK_WIDGET(opengl), &alloc);
	double width  = alloc.width;
	double height = alloc.height;
	double ang    = atan((height/2)/FOV_DIST)*2;
	double atmos  = 10000;
	double near   = MAX(elev*0.75 - atmos, 50);  // View 100km of atmosphere
	double far    = elev + EARTH_R*1.25 + atmos; // a bit past the cenrt of the earth

	glViewport(0, 0, width, height);
	gluPerspective(rad2deg(ang), width/height, near, far);

	/* Setup camera and lighting */
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	/* Camera 1 */
	glRotatef(rx, 1, 0, 0);
	glRotatef(rz, 0, 0, 1);

	/* Lighting */
	float light_position[] = {-13*EARTH_R, 1*EARTH_R, 3*EARTH_R, 1.0f};
	glLightfv(GL_LIGHT0, GL_POSITION, light_position);

	/* Camera 2 */
	glTranslatef(0, 0, -elev2rad(elev));
	glRotatef(lat, 1, 0, 0);
	glRotatef(-lon, 0, 1, 0);

	/* Update roam view */
	g_mutex_lock(&opengl->sphere_lock);
	roam_sphere_update_view(opengl->sphere);
	g_mutex_unlock(&opengl->sphere_lock);
}

static void _set_settings(GritsOpenGL *opengl)
{
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);

	glEnable(GL_LIGHT0);
	glEnable(GL_LIGHTING);

	glEnable(GL_LINE_SMOOTH);

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_COLOR_MATERIAL);

	if (opengl->wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	else
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	/* Lighting */
#ifdef ROAM_DEBUG
	float light_ambient[]  = {0.7f, 0.7f, 0.7f, 1.0f};
	float light_diffuse[]  = {2.0f, 2.0f, 2.0f, 1.0f};
#else
	float light_ambient[]  = {0.2f, 0.2f, 0.2f, 1.0f};
	float light_diffuse[]  = {0.8f, 0.8f, 0.8f, 1.0f};
#endif
	glLightfv(GL_LIGHT0, GL_AMBIENT,  light_ambient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE,  light_diffuse);

	/* Materials */
	float material_ambient[]  = {1.0, 1.0, 1.0, 1.0};
	float material_diffuse[]  = {1.0, 1.0, 1.0, 1.0};
	float material_specular[] = {0.0, 0.0, 0.0, 1.0};
	float material_emission[] = {0.0, 0.0, 0.0, 1.0};
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT,  material_ambient);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE,  material_diffuse);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, material_specular);
	glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, material_emission);

#ifdef ROAM_DEBUG
	glColor4f(1.0, 1.0, 1.0, 1.0);
	glLineWidth(2);
#else
	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);

	glClearDepth(1.0);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_DEPTH_TEST);
#endif
}

static GPtrArray *_objects_to_array(GritsOpenGL *opengl, gboolean ortho)
{
	GPtrArray *array = g_ptr_array_new();
	for (GList *i = opengl->objects->head; i; i = i->next) {
		struct RenderLevel *level = i->data;
		if ((ortho == TRUE  && level->num <  GRITS_LEVEL_HUD) ||
		    (ortho == FALSE && level->num >= GRITS_LEVEL_HUD))
			continue;
		for (GList *j = level->unsorted.next; j; j = j->next)
			g_ptr_array_add(array, j->data);
		for (GList *j = level->sorted.next;   j; j = j->next)
			g_ptr_array_add(array, j->data);
	}
	return array;
}

/*************
 * Callbacks *
 *************/

static gint run_picking(GritsOpenGL *opengl, GdkEvent *event,
		GPtrArray *objects, GritsObject **top)
{
	/* Setup picking buffers */
	guint buffer[100][4] = {};
	glSelectBuffer(G_N_ELEMENTS(buffer), (guint*)buffer);
	if (!opengl->pickmode)
		glRenderMode(GL_SELECT);
	glInitNames();

	/* Render/pick objects */
	for (guint i = 0; i < objects->len; i++) {
		glPushName(i);
		GritsObject *object = objects->pdata[i];
		object->state.picked = FALSE;
		grits_object_pick(object, opengl);
		glPopName();
	}

	int hits = glRenderMode(GL_RENDER);

	/* Process hits */
	for (int i = 0; i < hits; i++) {
		//g_debug("\tHit: %d",     i);
		//g_debug("\t\tcount: %d", buffer[i][0]);
		//g_debug("\t\tz1:    %f", (float)buffer[i][1]/0x7fffffff);
		//g_debug("\t\tz2:    %f", (float)buffer[i][2]/0x7fffffff);
		//g_debug("\t\tname:  %p", (gpointer)buffer[i][3]);
		guint        index  = buffer[i][3];
		GritsObject *object = objects->pdata[index];
		object->state.picked = TRUE;
		*top = object;
	}

	/* Notify objects of pointer movements */
	for (guint i = 0; i < objects->len; i++) {
		GritsObject *object = objects->pdata[i];
		grits_object_set_pointer(object, event, object->state.picked);
	}

	return hits;
}

static gboolean run_mouse_move(GritsOpenGL *opengl, GdkEventMotion *event)
{
	GtkAllocation alloc;
	gtk_widget_get_allocation(GTK_WIDGET(opengl), &alloc);

	gdouble gl_x   = event->x;
	gdouble gl_y   = alloc.height - event->y;
	gdouble delta  = opengl->pickmode ? 200 : 2;

	if (opengl->pickmode) {
		gtk_gl_begin(GTK_WIDGET(opengl));
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	/* Save matricies */
	gdouble projection[16];
	gint    viewport[4]; // x=0,y=0,w,h
	glGetDoublev(GL_PROJECTION_MATRIX, projection);
	glGetIntegerv(GL_VIEWPORT, viewport);
	glMatrixMode(GL_MODELVIEW);  glPushMatrix();
	glMatrixMode(GL_PROJECTION); glPushMatrix();

	g_mutex_lock(&opengl->objects_lock);

	GritsObject *top = NULL;
	GPtrArray *ortho = _objects_to_array(opengl, TRUE);
	GPtrArray *world = _objects_to_array(opengl, FALSE);

	/* Run perspective picking */
	glMatrixMode(GL_PROJECTION); glLoadIdentity();
	gluPickMatrix(gl_x, gl_y, delta, delta, viewport);
	glMultMatrixd(projection);
	gint world_hits = run_picking(opengl, (GdkEvent*)event, world, &top);

	/* Run ortho picking */
	glMatrixMode(GL_PROJECTION); glLoadIdentity();
	gluPickMatrix(gl_x, gl_y, delta, delta, viewport);
	glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
	glOrtho(0, viewport[2], viewport[3], 0, 1000, -1000);
	gint ortho_hits = run_picking(opengl, (GdkEvent*)event, ortho, &top);

	/* Update cursor */
	static GdkCursor *cursor = NULL;
	static GdkWindow *window = NULL;
	if (!window || !cursor) {
		cursor = gdk_cursor_new(GDK_FLEUR);
		window = gtk_widget_get_window(GTK_WIDGET(opengl));
	}
	GdkCursor *topcursor = top && top->cursor ? top->cursor : cursor;
	gdk_window_set_cursor(window, topcursor);

	g_debug("GritsOpenGL: run_mouse_move - hits=%d/%d,%d/%d ev=%.0lf,%.0lf",
			world_hits, world->len, ortho_hits, ortho->len, gl_x, gl_y);

	g_ptr_array_free(world, TRUE);
	g_ptr_array_free(ortho, TRUE);

	g_mutex_unlock(&opengl->objects_lock);

	/* Test unproject */
	//gdouble lat, lon, elev;
	//grits_viewer_unproject(GRITS_VIEWER(opengl),
	//		gl_x, gl_y, -1, &lat, &lon, &elev);

	/* Cleanup */
	glMatrixMode(GL_PROJECTION); glPopMatrix();
	glMatrixMode(GL_MODELVIEW);  glPopMatrix();

	if (opengl->pickmode)
		gtk_gl_end(GTK_WIDGET(opengl));

	return FALSE;
}

static gboolean on_motion_notify(GritsOpenGL *opengl, GdkEventMotion *event, gpointer _)
{
	opengl->mouse_queue = *event;
	grits_viewer_queue_draw(GRITS_VIEWER(opengl));
	return FALSE;
}

static void _draw_level(gpointer _level, gpointer _opengl)
{
	GritsOpenGL *opengl = _opengl;
	struct RenderLevel *level = _level;

	g_debug("GritsOpenGL: _draw_level - level=%-4d", level->num);
	int nsorted = 0, nunsorted = 0;
	GList *cur = NULL;

	/* Configure individual levels */
	if (level->num < GRITS_LEVEL_WORLD) {
		/* Disable depth for background levels */
		glDepthMask(FALSE);
		glDisable(GL_ALPHA_TEST);
	} else if (level->num < GRITS_LEVEL_OVERLAY) {
		/* Enable depth and alpha for world levels */
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.1);
		glDepthRange(OVERLAY_SLICE, 1);
	} else {
		/* Draw overlay in front of world */
		glDepthRange(0, OVERLAY_SLICE);
	}

	/* Start ortho */
	if (level->num >= GRITS_LEVEL_HUD) {
		GtkAllocation alloc;
		gtk_widget_get_allocation(GTK_WIDGET(opengl), &alloc);
		glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
		glOrtho(0, alloc.width, alloc.height, 0, 1000, -1000);
	}

	/* Draw unsorted objects without depth testing,
	 * these are polygons, etc, rather than physical objects */
	glDisable(GL_DEPTH_TEST);
	for (cur = level->unsorted.next; cur; cur = cur->next, nunsorted++)
		grits_object_draw(GRITS_OBJECT(cur->data), opengl);

	/* Draw sorted objects using depth testing
	 * These are things that are actually part of the world */
	glEnable(GL_DEPTH_TEST);
	for (cur = level->sorted.next; cur; cur = cur->next, nsorted++)
		grits_object_draw(GRITS_OBJECT(cur->data), opengl);

	/* End ortho */
	if (level->num >= GRITS_LEVEL_HUD) {
		glMatrixMode(GL_PROJECTION); glPopMatrix();
		glMatrixMode(GL_MODELVIEW);  glPopMatrix();
	}

	/* Leave depth buffer write enabled */
	glDepthMask(TRUE);

	/* TODO: Prune empty levels */

	g_debug("GritsOpenGL: _draw_level - drew %d,%d objects",
			nunsorted, nsorted);
}

static gboolean on_expose(GritsOpenGL *opengl, gpointer data, gpointer _)
{
	g_debug("GritsOpenGL: on_expose - begin");

	if (opengl->pickmode)
		return run_mouse_move(opengl, &(GdkEventMotion){});

	if (opengl->mouse_queue.type != GDK_NOTHING) {
		run_mouse_move(opengl, &opengl->mouse_queue);
		opengl->mouse_queue.type = GDK_NOTHING;
	}

	gtk_gl_begin(GTK_WIDGET(opengl));

	_set_settings(opengl);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

#ifndef ROAM_DEBUG
	g_mutex_lock(&opengl->sphere_lock);
	roam_sphere_update_errors(opengl->sphere);
	roam_sphere_split_merge(opengl->sphere);
	g_mutex_unlock(&opengl->sphere_lock);
#endif

#ifdef ROAM_DEBUG
	roam_sphere_draw(opengl->sphere);
	roam_sphere_draw_normals(opengl->sphere);
	(void)_draw_level;
#else
	g_mutex_lock(&opengl->objects_lock);
	g_queue_foreach(opengl->objects, _draw_level, opengl);
	g_mutex_unlock(&opengl->objects_lock);
#endif

	gtk_gl_end(GTK_WIDGET(opengl));

	g_debug("GritsOpenGL: on_expose - end\n");
	return FALSE;
}

static gboolean on_key_press(GritsOpenGL *opengl, GdkEventKey *event, gpointer _)
{
	g_debug("GritsOpenGL: on_key_press - key=%x, state=%x, plus=%x",
			event->keyval, event->state, GDK_KEY_plus);

	guint kv = event->keyval;
	/* Testing */
	if (kv == GDK_KEY_w) {
		opengl->wireframe = !opengl->wireframe;
		grits_viewer_queue_draw(GRITS_VIEWER(opengl));
	}
	if (kv == GDK_KEY_p) {
		opengl->pickmode = !opengl->pickmode;
		grits_viewer_queue_draw(GRITS_VIEWER(opengl));
	}
#ifdef ROAM_DEBUG
	else if (kv == GDK_KEY_n) roam_sphere_split_one(opengl->sphere);
	else if (kv == GDK_KEY_p) roam_sphere_merge_one(opengl->sphere);
	else if (kv == GDK_KEY_r) roam_sphere_split_merge(opengl->sphere);
	else if (kv == GDK_KEY_u) roam_sphere_update_errors(opengl->sphere);
	grits_viewer_queue_draw(GRITS_VIEWER(opengl));
#endif
	return FALSE;
}

static gboolean on_chained_event(GritsOpenGL *opengl, GdkEvent *event, gpointer _)
{
	for (GList *i = opengl->objects->tail; i; i = i->prev) {
		struct RenderLevel *level = i->data;
		for (GList *j = level->unsorted.next; j; j = j->next)
			if (grits_object_event(j->data, event))
				return TRUE;
		for (GList *j = level->sorted.next;   j; j = j->next)
			if (grits_object_event(j->data, event))
				return TRUE;
	}
	return FALSE;
}

static void on_realize(GritsOpenGL *opengl, gpointer _)
{
	g_debug("GritsOpenGL: on_realize");
	gtk_gl_begin(GTK_WIDGET(opengl));

	/* Connect signals and idle functions now that opengl is fully initialized */
	gtk_widget_add_events(GTK_WIDGET(opengl), GDK_KEY_PRESS_MASK);
	g_signal_connect(opengl, "configure-event",  G_CALLBACK(_set_projection), NULL);
#if GTK_CHECK_VERSION(3,0,0)
	g_signal_connect(opengl, "draw",             G_CALLBACK(on_expose),       NULL);
#else
	g_signal_connect(opengl, "expose-event",     G_CALLBACK(on_expose),       NULL);
#endif

	g_signal_connect(opengl, "key-press-event",  G_CALLBACK(on_key_press),    NULL);

	g_signal_connect(opengl, "location-changed", G_CALLBACK(_set_projection), NULL);
	g_signal_connect(opengl, "rotation-changed", G_CALLBACK(_set_projection), NULL);

	g_signal_connect(opengl, "motion-notify-event", G_CALLBACK(on_motion_notify), NULL);
	g_signal_connect_after(opengl, "key-press-event",      G_CALLBACK(on_chained_event), NULL);
	g_signal_connect_after(opengl, "key-release-event",    G_CALLBACK(on_chained_event), NULL);
	g_signal_connect_after(opengl, "button-press-event",   G_CALLBACK(on_chained_event), NULL);
	g_signal_connect_after(opengl, "button-release-event", G_CALLBACK(on_chained_event), NULL);
	g_signal_connect_after(opengl, "motion-notify-event",  G_CALLBACK(on_chained_event), NULL);

	/* Re-queue resize incase configure was triggered before realize */
	gtk_widget_queue_resize(GTK_WIDGET(opengl));
}

/*********************
 * GritsViewer methods *
 *********************/
/**
 * grits_opengl_new:
 * @plugins: the plugins store to use
 * @prefs:   the preferences object to use
 *
 * Create a new OpenGL renderer.
 *
 * Returns: the new #GritsOpenGL
 */
GritsViewer *grits_opengl_new(GritsPlugins *plugins, GritsPrefs *prefs)
{
	g_debug("GritsOpenGL: new");
	GritsViewer *opengl = g_object_new(GRITS_TYPE_OPENGL, NULL);
	grits_viewer_setup(opengl, plugins, prefs);
	return opengl;
}

static void grits_opengl_center_position(GritsViewer *_opengl, gdouble lat, gdouble lon, gdouble elev)
{
	glRotatef(lon, 0, 1, 0);
	glRotatef(-lat, 1, 0, 0);
	glTranslatef(0, 0, elev2rad(elev));
}

static void grits_opengl_project(GritsViewer *_opengl,
		gdouble lat, gdouble lon, gdouble elev,
		gdouble *px, gdouble *py, gdouble *pz)
{
	GritsOpenGL *opengl = GRITS_OPENGL(_opengl);
	gdouble x, y, z;
	lle2xyz(lat, lon, elev, &x, &y, &z);
	gluProject(x, y, z,
		opengl->sphere->view->model,
		opengl->sphere->view->proj,
		opengl->sphere->view->view,
		px, py, pz);
}

static void grits_opengl_unproject(GritsViewer *_opengl,
		gdouble px, gdouble py, gdouble pz,
		gdouble *lat, gdouble *lon, gdouble *elev)
{
	GritsOpenGL *opengl = GRITS_OPENGL(_opengl);
	if (!opengl->sphere->view)
		return;
	gdouble x, y, z;
	if (pz < 0) {
		gfloat tmp = 0;
		glReadPixels(px, py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &tmp);
		pz = tmp;
	}
	pz = (pz-OVERLAY_SLICE) * (1.0/(1-OVERLAY_SLICE));
	gluUnProject(px, py, pz,
		opengl->sphere->view->model,
		opengl->sphere->view->proj,
		opengl->sphere->view->view,
		&x, &y, &z);
	xyz2lle(x, y, z, lat, lon, elev);
	//g_message("GritsOpenGL: unproject - "
	//		"%4.0lf,%4.0lf,(%5.3lf) -> "
	//		"%8.0lf,%8.0lf,%8.0lf -> "
	//		"%6.2lf,%7.2lf,%4.0lf",
	//	px, py, pz, x, y, z, *lat, *lon, *elev);
}

static void grits_opengl_set_height_func(GritsViewer *_opengl, GritsBounds *bounds,
		RoamHeightFunc height_func, gpointer user_data, gboolean update)
{
	GritsOpenGL *opengl = GRITS_OPENGL(_opengl);
	/* TODO: get points? */
	g_mutex_lock(&opengl->sphere_lock);
	GList *triangles = roam_sphere_get_intersect(opengl->sphere, TRUE,
			bounds->n, bounds->s, bounds->e, bounds->w);
	for (GList *cur = triangles; cur; cur = cur->next) {
		RoamTriangle *tri = cur->data;
		RoamPoint *points[] = {tri->p.l, tri->p.m, tri->p.r, tri->split};
		for (int i = 0; i < G_N_ELEMENTS(points); i++) {
			if (bounds->n >= points[i]->lat && points[i]->lat >= bounds->s &&
			    bounds->e >= points[i]->lon && points[i]->lon >= bounds->w) {
				points[i]->height_func = height_func;
				points[i]->height_data = user_data;
				roam_point_update_height(points[i]);
			}
		}
	}
	g_list_free(triangles);
	g_mutex_unlock(&opengl->sphere_lock);
}

static void _grits_opengl_clear_height_func_rec(RoamTriangle *root)
{
	if (!root)
		return;
	RoamPoint *points[] = {root->p.l, root->p.m, root->p.r, root->split};
	for (int i = 0; i < G_N_ELEMENTS(points); i++) {
		points[i]->height_func = NULL;
		points[i]->height_data = NULL;
		roam_point_update_height(points[i]);
	}
	_grits_opengl_clear_height_func_rec(root->kids[0]);
	_grits_opengl_clear_height_func_rec(root->kids[1]);
}

static void grits_opengl_clear_height_func(GritsViewer *_opengl)
{
	GritsOpenGL *opengl = GRITS_OPENGL(_opengl);
	for (int i = 0; i < G_N_ELEMENTS(opengl->sphere->roots); i++)
		_grits_opengl_clear_height_func_rec(opengl->sphere->roots[i]);
}

static gint _objects_find(gconstpointer a, gconstpointer b)
{
	const struct RenderLevel *level = a;
	const gint *key = b;
	return level->num == *key ? 0 : 1;
}

static gint _objects_sort(gconstpointer _a, gconstpointer _b, gpointer _)
{
	const struct RenderLevel *a = _a;
	const struct RenderLevel *b = _b;
	return a->num < b->num ? -1 :
	       a->num > b->num ?  1 : 0;
}

static void _objects_free(gpointer value, gpointer _)
{
	struct RenderLevel *level = value;
	if (level->sorted.next)
		g_list_free_full(level->sorted.next, g_object_unref);
	if (level->unsorted.next)
		g_list_free_full(level->unsorted.next, g_object_unref);
	g_free(level);
}

static void grits_opengl_add(GritsViewer *_opengl, GritsObject *object,
		gint num, gboolean sort)
{
	g_assert(GRITS_IS_OPENGL(_opengl));
	GritsOpenGL *opengl = GRITS_OPENGL(_opengl);
	g_mutex_lock(&opengl->objects_lock);
	struct RenderLevel *level = NULL;
	GList *tmp = g_queue_find_custom(opengl->objects, &num, _objects_find);
	if (tmp) {
		level = tmp->data;
	} else {
		level = g_new0(struct RenderLevel, 1);
		level->num = num;
		g_queue_insert_sorted(opengl->objects, level, _objects_sort, NULL);
	}
	GList *list = sort ? &level->sorted : &level->unsorted;
	/* Put the link in the list */
	GList *link = g_new0(GList, 1);
	link->data = object;
	link->prev = list;
	link->next = list->next;
	if (list->next)
		list->next->prev = link;
	list->next = link;
	object->ref = link;
	g_object_ref(object);
	g_mutex_unlock(&opengl->objects_lock);
}

void grits_opengl_remove(GritsViewer *_opengl, GritsObject *object)
{
	g_assert(GRITS_IS_OPENGL(_opengl));
	GritsOpenGL *opengl = GRITS_OPENGL(_opengl);
	if (!object->ref)
		return;
	g_mutex_lock(&opengl->objects_lock);
	GList *link = object->ref;
	/* Just unlink and free it, link->prev is assured */
	link->prev->next = link->next;
	if (link->next)
		link->next->prev = link->prev;
	g_free(link);
	object->ref = NULL;
	g_object_unref(object);
	g_mutex_unlock(&opengl->objects_lock);
}

/****************
 * GObject code *
 ****************/
G_DEFINE_TYPE(GritsOpenGL, grits_opengl, GRITS_TYPE_VIEWER);
static void grits_opengl_init(GritsOpenGL *opengl)
{
	g_debug("GritsOpenGL: init");
	opengl->objects = g_queue_new();
	opengl->sphere  = roam_sphere_new(opengl);
	g_mutex_init(&opengl->objects_lock);
	g_mutex_init(&opengl->sphere_lock);
	gtk_gl_enable(GTK_WIDGET(opengl));
	gtk_widget_add_events(GTK_WIDGET(opengl), GDK_KEY_PRESS_MASK);
	g_signal_connect(opengl, "map", G_CALLBACK(on_realize), NULL);
}
static void grits_opengl_dispose(GObject *_opengl)
{
	g_debug("GritsOpenGL: dispose");
	GritsOpenGL *opengl = GRITS_OPENGL(_opengl);
	if (opengl->objects) {
		GQueue *objects = opengl->objects;;
		opengl->objects = NULL;
		g_mutex_lock(&opengl->objects_lock);
		g_queue_foreach(objects, _objects_free, NULL);
		g_queue_free(objects);
		g_mutex_unlock(&opengl->objects_lock);
	}
	G_OBJECT_CLASS(grits_opengl_parent_class)->dispose(_opengl);
}
static void grits_opengl_finalize(GObject *_opengl)
{
	g_debug("GritsOpenGL: finalize");
	GritsOpenGL *opengl = GRITS_OPENGL(_opengl);
	roam_sphere_free(opengl->sphere);
	g_mutex_clear(&opengl->objects_lock);
	g_mutex_clear(&opengl->sphere_lock);
	gtk_gl_disable(GTK_WIDGET(opengl));
	G_OBJECT_CLASS(grits_opengl_parent_class)->finalize(_opengl);
}
static void grits_opengl_class_init(GritsOpenGLClass *klass)
{
	g_debug("GritsOpenGL: class_init");
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->finalize = grits_opengl_finalize;
	gobject_class->dispose = grits_opengl_dispose;

	GritsViewerClass *viewer_class = GRITS_VIEWER_CLASS(klass);
	viewer_class->center_position   = grits_opengl_center_position;
	viewer_class->project           = grits_opengl_project;
	viewer_class->unproject         = grits_opengl_unproject;
	viewer_class->clear_height_func = grits_opengl_clear_height_func;
	viewer_class->set_height_func   = grits_opengl_set_height_func;
	viewer_class->add               = grits_opengl_add;
	viewer_class->remove            = grits_opengl_remove;
}
