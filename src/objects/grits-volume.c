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
 * SECTION:grits-volume
 * @short_description: 3-D gridded vovlume
 *
 * Each #GritsVolume consistes of a 3-dimentional grid of data points each
 * consisting of a value.
 *
 * Currently iso-surfaces are extracted and displayed when rendering the volume
 * data.
 */

#include <config.h>
#include <math.h>
#include <glib.h>
#include <stdatomic.h>
#include "gtkgl.h"
#include "grits-volume.h"

/* Drawing */
static void draw_points(VolGrid *grid, gdouble level)
{
	glPointSize(200./grid->xs);
	glBegin(GL_POINTS);
	for (int x = 0; x < grid->xs; x++)
	for (int y = 0; y < grid->ys; y++)
	for (int z = 0; z < grid->zs; z++) {
		VolPoint *pt = vol_grid_get(grid, x, y, z);
		if (pt->value < level)
			continue;

		g_debug("(%d,%d,%d) value=%f", x, y, z, pt->value);

		glColor4f(1.0, 1.0, 1.0, pt->value/100);
		glVertex3dv((double*)&pt->c);
	}
	glEnd();
}

static void draw_iso(GritsVolume* volume)
{
	g_debug("GritsVolume: draw_iso");

	GList* tris = volume->tris;

	/* Update the tris in use for drawing flag so we don't attempt to delete this array if we are updating the tris list in another thread */
	volume->trisInUseForDrawing = tris;

	glDisable(GL_CULL_FACE);
	glBegin(GL_TRIANGLES);
	for (GList *cur = tris; cur; cur = cur->next) {
		VolTriangle *tri = cur->data;
		VolCoord c[3] = {tri->v[0]->c,    tri->v[1]->c,    tri->v[2]->c   };
		VolCoord n[3] = {tri->v[0]->norm, tri->v[1]->norm, tri->v[2]->norm};

		/* Normalize normal vector */
		for (int i = 0; i < 3; i++) {
			double total = sqrt(
				n[i].x * n[i].x +
				n[i].y * n[i].y +
				n[i].z * n[i].z);
			if (total == 0)
				continue; // wtf
			n[i].x = n[i].x / total;
			n[i].y = n[i].y / total;
			n[i].z = n[i].z / total;
		}

		//for (int i = 0; i < 3; i++)
		//	g_debug("norm=%f,%f,%f",
		//		n[i].x, n[i].y, n[i].z);
		//glNormal3dv((double*)&tri->norm);
		glNormal3dv((double*)&n[0]); glVertex3dv((double*)&c[0]);
		glNormal3dv((double*)&n[1]); glVertex3dv((double*)&c[1]);
		glNormal3dv((double*)&n[2]); glVertex3dv((double*)&c[2]);
	}
	glEnd();

	volume->trisInUseForDrawing = NULL;
}

static void draw(GritsObject *_volume, GritsOpenGL *opengl)
{
	g_debug("GritsVolume: draw");
	GritsVolume *volume = GRITS_VOLUME(_volume);

	gfloat amb[4] = {};
	gfloat dif[4] = {};
	switch (volume->disp) {
	case GRITS_VOLUME_SURFACE:
		glDisable(GL_COLOR_MATERIAL);
		amb[0] = (double)volume->color[0] / 0xff;
		amb[1] = (double)volume->color[1] / 0xff;
		amb[2] = (double)volume->color[2] / 0xff;
		amb[3] = 1;
		dif[0] = (double)volume->color[0] / 0xff;
		dif[1] = (double)volume->color[1] / 0xff;
		dif[2] = (double)volume->color[2] / 0xff;
		dif[3] = 1;
		glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, amb);
		glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, dif);
		draw_iso(volume);
		break;
	case GRITS_VOLUME_POINTS:
		draw_points(volume->grid, volume->level);
		break;
	}
}

/* Idle update */
gboolean update_iso(gpointer _volume)
{
	GritsVolume *volume = _volume;

	/* Generate new isosurface and point volume->tris to it atomically, then point oldTris to the old version of volume->tris so we can clean it up.
	 * This allows us to generate a new isosurface in a non-UI thread and safely swap it over. */
	GList* oldTris = g_atomic_pointer_exchange(&volume->tris, marching_cubes(volume->grid, volume->level));

	if (oldTris) {
		/* Spin lock wait for the drawing to complete so we can delete this array.
		 * Drawing should be very fast and the chance of us trying to delete the list while drawing is very small, but just in case,
		 * we will spin lock wait for the list to be no longer in use so we can delete it.
		 * We use atomic_load on volume->trisInUseForDrawing since without it, the compiler would read the value once and cache it,
		 * causing the loop below to get stuck.
		 */
		while(oldTris == atomic_load(&volume->trisInUseForDrawing)){};

		g_list_foreach(oldTris, (GFunc)vol_triangle_free, NULL);
		g_list_free(oldTris);
	}
	volume->update_id = 0;
	grits_object_queue_draw(GRITS_OBJECT(volume));
	return FALSE;
}

/***********
 * Methods *
 ***********/
void grits_volume_set_level(GritsVolume *volume, gdouble level)
{
	volume->level = level;
	if (!volume->update_id)
		volume->update_id = g_idle_add(update_iso, volume);
}

void grits_volume_set_level_sync(GritsVolume *volume, gdouble level){
	volume->level = level;
	update_iso(volume);
}

GritsVolume *grits_volume_new(VolGrid *grid)
{
	g_debug("GritsVolume: new - %p[%d][%d][%d]",
			grid, grid->xs, grid->ys, grid->zs);
	GritsVolume *volume = g_object_new(GRITS_TYPE_VOLUME, NULL);
	volume->grid = grid;
	return volume;
}

/* GObject code */
G_DEFINE_TYPE(GritsVolume, grits_volume, GRITS_TYPE_OBJECT);
static void grits_volume_init(GritsVolume *volume)
{
}

static void grits_volume_finalize(GObject *_volume)
{
	GritsVolume *volume = GRITS_VOLUME(_volume);
	volume->color[0] = 1;
	volume->color[1] = 1;
	volume->color[2] = 1;
	volume->color[3] = 1;
	//g_debug("GritsVolume: finalize - %s", volume->label);
}

static void grits_volume_class_init(GritsVolumeClass *klass)
{
	g_debug("GritsVolume: class_init");
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->finalize = grits_volume_finalize;

	GritsObjectClass *object_class = GRITS_OBJECT_CLASS(klass);
	object_class->draw = draw;
}
