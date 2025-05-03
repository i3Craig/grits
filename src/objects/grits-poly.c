/*
 * Copyright (C) 2010-2011 Andy Spencer <andy753421@gmail.com>
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
 * SECTION:grits-poly
 * @short_description: Single point polys
 *
 * Each #GritsPoly represents a 3 dimentional polygon.
 */

#include <config.h>
#include <stdbool.h>
#include <GL/glew.h>
#include "gtkgl.h"
#include "grits-poly.h"

/* User data object we use to store state collected from the tessellation callback functions */
typedef struct {
	/* Stres the array of vertices in the triangles in this tessellation */
	GArray* objTessellationTriangleVertices;
	/* Current mode that we will receive vertices from the tessellation object in. One of GL_TRIANGLE_FAN, GL_TRIANGLE_STRIP, GL_TRIANGLE */
	GLenum iVertexAddMode;
	/* Stores the index in the array at which the current vertex add mode started */
	guint iStartIndexForCurrentAddMode;
} TessellationUserData;

/* Add another vertex to the current triangle or start a new triangle in the current tessellation */
static void _grits_tess_add_vertex(void* _vertex, void* ipobjTessellationUserData){
	TessellationUserData* objTessellationUserData = (TessellationUserData*) ipobjTessellationUserData;
	guint iIndexRelativeToStartOfMode = objTessellationUserData->objTessellationTriangleVertices->len - objTessellationUserData->iStartIndexForCurrentAddMode;
	guint iIndexOfPreviousVertex = objTessellationUserData->objTessellationTriangleVertices->len - 1;
	Vertex* objVertex = (Vertex*) _vertex;

	switch(objTessellationUserData->iVertexAddMode){
		case GL_TRIANGLE_FAN:
			if(iIndexRelativeToStartOfMode > 2){
				Vertex objFanCenterPoint = g_array_index(objTessellationUserData->objTessellationTriangleVertices, Vertex, objTessellationUserData->iStartIndexForCurrentAddMode);
				Vertex objFanPreviousPoint = g_array_index(objTessellationUserData->objTessellationTriangleVertices, Vertex, iIndexOfPreviousVertex);

				/* Add 'center point' to all triangles */
				g_array_append_val(objTessellationUserData->objTessellationTriangleVertices, objFanCenterPoint);
				/* Add previous point to this triangle */
				g_array_append_val(objTessellationUserData->objTessellationTriangleVertices, objFanPreviousPoint);
			}
			break;
		case GL_TRIANGLE_STRIP:
			if(iIndexRelativeToStartOfMode > 2){
				Vertex objStripPreviousPreviousPoint = g_array_index(objTessellationUserData->objTessellationTriangleVertices, Vertex, iIndexOfPreviousVertex - 1);
				Vertex objStripPreviousPoint = g_array_index(objTessellationUserData->objTessellationTriangleVertices, Vertex, iIndexOfPreviousVertex);

				/* Add previous previous point to this triangle */
				g_array_append_val(objTessellationUserData->objTessellationTriangleVertices, objStripPreviousPreviousPoint);
				/* Add previous point to this triangle */
				g_array_append_val(objTessellationUserData->objTessellationTriangleVertices, objStripPreviousPoint);
			}
			break;
	} /* switch(objTessellationUserData->iVertexAddMode){ */

	/* Add the vertex from this tessellation callback */
	g_array_append_val(objTessellationUserData->objTessellationTriangleVertices, *objVertex);
}

/* Called when the tessellation logic decides to use a new mode of sending triangle data to us */
static void _grits_tess_begin(GLenum type, void* ipobjTessellationUserData){
	TessellationUserData* objTessellationUserData = (TessellationUserData*) ipobjTessellationUserData;
	/* Store the current mode so we know how to interpret the incoming tessellation vertices */
	objTessellationUserData->iVertexAddMode = type;
	objTessellationUserData->iStartIndexForCurrentAddMode = objTessellationUserData->objTessellationTriangleVertices->len;
}

/* Called when we are done getting tessellation data in one particular mode */
static void _grits_tess_end(void* ipobjTessellationUserData){
}

static void grits_poly_tess(GritsPoly *poly) {
	TessellationUserData* objTessellationUserData = g_malloc0(sizeof(TessellationUserData));
	objTessellationUserData->objTessellationTriangleVertices = g_array_new(false, false, sizeof(Vertex));
	GLUtesselator *tess = gluNewTess();

	gluTessCallback(tess, GLU_TESS_VERTEX_DATA, (void*) _grits_tess_add_vertex);
	gluTessCallback(tess, GLU_TESS_BEGIN_DATA, (void*) _grits_tess_begin);
	gluTessCallback(tess, GLU_TESS_END_DATA, (void*) _grits_tess_end);

	/* Send vertices point by point to the tessellation object */
	for (int pi = 0; poly->points[pi]; pi++) {
		gluTessBeginPolygon(tess, (void*) objTessellationUserData);
		gluTessBeginContour(tess);
		for (int ci = 0; poly->points[pi][ci][0]; ci++) {
			gluTessVertex(tess, poly->points[pi][ci], poly->points[pi][ci]);
		}
		gluTessEndContour(tess);
		gluTessEndPolygon(tess);
	}
	gluDeleteTess(tess);

	/* Count the number of triangles in this tessellation and get the underlying array so we can upload that to the GPU. */
	poly->iTessVertexCount = objTessellationUserData->objTessellationTriangleVertices->len;
	Vertex* aTessVertices = (Vertex*) g_array_free(objTessellationUserData->objTessellationTriangleVertices, false /* don't free array segment */ );

	/* Upload the tessellation to the GPU as a vertex buffer object */
	glGenBuffers(1, &poly->iTessVbo);
	glBindBuffer(GL_ARRAY_BUFFER, poly->iTessVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * poly->iTessVertexCount, aTessVertices, GL_STATIC_DRAW);

	g_free(aTessVertices);
	g_free(objTessellationUserData);
}

/* Draw the outline of the polygon(s) in this polygon class instance. */
static void grits_poly_outline(GritsPoly *poly) {
	glBindVertexArray(poly->iOutlineVao);
	glDrawElements(GL_LINE_LOOP, poly->iOutlineIndicesLength, GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
}

/* Draw the fill of this polygon. This requires the tessellation to be uploaded to the GPU before calling this function. See grits_poly_tess */
static void grits_poly_fill(GritsPoly *poly){
	glEnableClientState(GL_VERTEX_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, poly->iTessVbo);
	glVertexPointer(3, GL_DOUBLE, 0, 0);
	glDrawArrays(GL_TRIANGLES, 0, poly->iTessVertexCount);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void grits_poly_init_outline_buffers(GritsPoly *poly) {
	glGenVertexArrays(1, &poly->iOutlineVao);
	glGenBuffers(1, &poly->iOutlineVbo);
	glBindVertexArray(poly->iOutlineVao);
	glBindBuffer(GL_ARRAY_BUFFER, poly->iOutlineVbo);

	/* Count vertices and allocate buffer */
	int iPointsInPolygon = 0;
	GLuint iOutlineDisttinctObjectsCnt = 0;
	for (int pi = 0; poly->points[pi]; pi++) {
		/* We found a new (distinct) polygon in this "poly" object; increment the poly counter so it is rendered seperately */
		iOutlineDisttinctObjectsCnt++;

		for (int ci = 0; poly->points[pi][ci][0]; ci++) {
			iPointsInPolygon++;
		}
	}

	/* Build a list of all points in this / these polygon(s). Some GritsPolygons can have multiple distinct polygons.
	 * We combine them into one so we can save it in one Vertex Buffer Object, allowing us to draw it with one OpenGL call.
	 */
	gdouble* aAllPoints = g_malloc0(sizeof(gdouble) * 3 * iPointsInPolygon);

	/* Build a list of all indices in this polygon (plus an extra index for each delimiter between polygons for the primitive restart)
	 * - including disconnected polygons.
	 */
	poly->iOutlineIndicesLength = iPointsInPolygon;
	if(iOutlineDisttinctObjectsCnt > 1){
		/* If there are two or more objects (N) in the GritsPoly, then we need N - 1 delimiters. */
		poly->iOutlineIndicesLength += iOutlineDisttinctObjectsCnt - 1;
	}
	GLuint* aIndices = g_malloc0(sizeof(GLuint) * poly->iOutlineIndicesLength);

	guint iAllPointsIndex = 0;
	GLuint iVertexIndex = 0;
	for (int pi = 0; poly->points[pi]; pi++) {
		if(pi > 0){
			/* This is the second (distinct) polygon in this GritsPoly class instance. Add a delimiter so OpenGL doesn't connect this polygon with the previous one */
			aIndices[iVertexIndex + pi - 1] = GRITS_POLY_PRIMITIVE_RESTART_INDEX_VALUE;
		}

		for (int ci = 0; poly->points[pi][ci][0]; ci++){
			for(int di = 0; di < 3; di++){
				/* Individually copy over the x, y, z components of this point into our single buffer */
				aAllPoints[iAllPointsIndex++] = poly->points[pi][ci][di];
			}

			/* Populate the indexes array */
			aIndices[iVertexIndex + pi] = iVertexIndex;

			iVertexIndex++;
		}
	}

	glBufferData(GL_ARRAY_BUFFER, iPointsInPolygon * 3 * sizeof(GLdouble), aAllPoints, GL_STATIC_DRAW);
	glVertexPointer(3, GL_DOUBLE, 0, 0);
	glEnableClientState(GL_VERTEX_ARRAY);


	/* Allocate space for and upload the Element Array Buffer to the GPU */
	glGenBuffers(1, &poly->iOutlineEbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, poly->iOutlineEbo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, poly->iOutlineIndicesLength * sizeof(GLuint), aIndices, GL_STATIC_DRAW);

	glVertexPointer(3, GL_DOUBLE, 0, 0);
	glEnableClientState(GL_VERTEX_ARRAY);

	/* This technically only needs to run once on program start. This tells OpenGL what to look for in the index array to know when to start a new polygon */
	glEnable(GL_PRIMITIVE_RESTART);
	glPrimitiveRestartIndex(GRITS_POLY_PRIMITIVE_RESTART_INDEX_VALUE);

	glBindVertexArray(0);

	/* The buffer of all points is now in GPU memory. We don't need it anymore */
	g_free(aAllPoints);
	g_free(aIndices);
}

static void grits_poly_draw(GritsObject *_poly, GritsOpenGL *opengl)
{
	//g_debug("GritsPoly: draw");
	GritsPoly *poly = GRITS_POLY(_poly);

	/* If we have not initialized the vertex buffer object for the outline of this poly, then initialize it */
	if(poly->iOutlineVbo == 0)
	  grits_poly_init_outline_buffers(poly);

	glPushAttrib(GL_COLOR_BUFFER_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT |
			GL_POINT_BIT | GL_LINE_BIT | GL_POLYGON_BIT);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_LIGHTING);

	/* Draw the fill if enabled */
	if (poly->color[3]) {
		glColor4dv(poly->color);
		if(poly->iTessVbo == 0){
			/* We didn't tessellate this polygon yet. Go ahead and do that now. */
			grits_poly_tess(poly);
		}
		grits_poly_fill(poly);
	}

	/* Draw a black border around the outline of this polygon to make it more visible */
	if (!poly->color[3] && poly->border[3] && poly->width > 1) {
		glColor4d(0,0,0,1);

		glPointSize(poly->width*2);
		glLineWidth(poly->width*2);

		grits_poly_outline(poly);
	}

	/* Draw the outline if enabled */
	if (poly->border[3]) {
		glColor4dv(poly->border);
		glPointSize(poly->width);
		glLineWidth(poly->width);
		grits_poly_outline(poly);
	}

	glPopAttrib();
}


/* Called when we are in "pick" mode ( glRenderMode(GL_SELECT) ) to determine if the user has clicked this GritsPoly or not */
static void grits_poly_pick(GritsObject *_poly, GritsOpenGL *opengl)
{
	//g_debug("GritsPoly: pick");
	GritsPoly *poly = GRITS_POLY(_poly);
	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_CULL_FACE);

	if(poly->iTessVbo == 0){
		/* We didn't tessellate this polygon yet. Go ahead and do that now. */
		grits_poly_tess(poly);
	}
	/* Here, we only draw the fill of the polygon as we only want to report the the user clicked the polygon if they clicked inside of the fill */
	grits_poly_fill(poly);

	glPopAttrib();
}



/* Legacy methods used for OpenGL 3.0 / OpenGL ES 3.0 and older support, as glPrimitiveRestartIndex requires OpenGL 3.1 or newer.
 * These use immediate mode to draw outlines, which can be slow compared to modern methods. GL Runlists can speed this up when redrawing, but this is
 * not supported on OpenGL ES, so we don't use runlists here to maximize compatibility.
 */

static void grits_poly_outline_legacy(gdouble (**points)[3])
{
	//g_debug("GritsPoly: outline");
	for (int pi = 0; points[pi]; pi++) {
		//glBegin(GL_POLYGON);
		glBegin(GL_LINE_LOOP);
	 	for (int ci = 0; points[pi][ci][0] &&
	 	                 points[pi][ci][1] &&
	 	                 points[pi][ci][2]; ci++)
			glVertex3dv(points[pi][ci]);
		glEnd();
	}
}

static void grits_poly_draw_legacy(GritsObject *_poly, GritsOpenGL *opengl)
{
	//g_debug("GritsPoly: draw");
	GritsPoly *poly = GRITS_POLY(_poly);

	glPushAttrib(GL_COLOR_BUFFER_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT |
			GL_POINT_BIT | GL_LINE_BIT | GL_POLYGON_BIT);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_LIGHTING);

	glEnable(GL_POLYGON_OFFSET_FILL);
	glEnable(GL_POLYGON_OFFSET_LINE);
	glEnable(GL_POLYGON_OFFSET_POINT);

	/* Draw the fill if enabled */
	if (poly->color[3]) {
		glColor4dv(poly->color);
		if(poly->iTessVbo == 0){
			/* We didn't tessellate this polygon yet. Go ahead and do that now. */
			grits_poly_tess(poly);
		}
		grits_poly_fill(poly);
	}

	glEnable(GL_POLYGON_SMOOTH);
	glEnable(GL_LINE_SMOOTH);
	glEnable(GL_POINT_SMOOTH);

	if (!poly->color[3] && poly->border[3] && poly->width > 1) {
		/* Draw line border in the middle */
		glColor4d(0,0,0,1);

		glPointSize(poly->width*2);
		glLineWidth(poly->width*2);

		glPolygonOffset(2, 2);

		glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
		grits_poly_outline_legacy(poly->points);
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		grits_poly_outline_legacy(poly->points);
	}

	if (poly->border[3]) {
		/* Draw border front-most */
		glColor4dv(poly->border);

		glPointSize(poly->width);
		glLineWidth(poly->width);

		glPolygonOffset(1, 1);
		if (poly->width > 1) {
			glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
			grits_poly_outline_legacy(poly->points);
		}
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		grits_poly_outline_legacy(poly->points);
	}

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glPopAttrib();
}

/* End of legacy OpenGL methods */

/* Call to check if the current OpenGL version is equal to or greater than the passed in version */
static bool isGlVersionGreaterThanOrEqualTo(int ipiMajorVersion, int ipiMinorVersion){
	const char* versionStr = (const char*)glGetString(GL_VERSION);
	if (versionStr) {
		int major = 0, minor = 0;
		if (sscanf(versionStr, "OpenGL ES %d.%d", &major, &minor) == 2 ||
			sscanf(versionStr, "%d.%d", &major, &minor) == 2) {

			if ((major > ipiMajorVersion) || (major == ipiMajorVersion && minor >= ipiMinorVersion)) {
				return true;
			}
		}
	} else {
		g_warning("GL Version string is undefined. Unable to determine OpenGL version");
	}
	/* GL Version of this system is not at or above the requested version */
	return false;
}

/* There is only one instance of GritsObjectClass in this program. Below, we save off a pointer to it so we can update it at runtime.
 * That is, this method hooks on the first draw call made (thus, after OpenGL is initialized) to check which version of OpenGL is in use and updates the draw method pointer
 * to point to the method which can handle that version of OpenGL.
 * This eliminates a lot of unnecessary version checks as each polygon is rendered for each frame.
 */
static GritsObjectClass* objGritsObjectClassPointer;
static void grits_poly_draw_version_selector(GritsObject *_poly, GritsOpenGL *opengl){
	if(isGlVersionGreaterThanOrEqualTo(3, 1)){
		/* The new drawing logic requires OpenGL 3.1 or newer. It is much faster and should be used in all cases unless it is not supported */
		objGritsObjectClassPointer->draw = grits_poly_draw;
	} else {
		g_info("OpenGL version is less than 3.1. Using legacy renderer.");
		objGritsObjectClassPointer->draw = grits_poly_draw_legacy;
	}

	objGritsObjectClassPointer->draw(_poly, opengl);
}

/**
 * grits_poly_new:
 * @points: Array of polygons. Each polygon is an array of 3D points. Each 3D point is an array of 3 doubles.
 * @npoints: TODO
 *
 * Create a new GritsPoly which TODO.
 *
 * Returns: the new #GritsPoly.
 */
GritsPoly *grits_poly_new(gdouble (**points)[3])
{
	//g_debug("GritsPoly: new - %p", points);
	GritsPoly *poly = g_object_new(GRITS_TYPE_POLY, NULL);
	poly->points = points;
	return poly;
}

GritsPoly *grits_poly_parse(const gchar *str,
		const gchar *poly_sep, const gchar *point_sep, const gchar *coord_sep)
{
	GritsPoint center;
	gdouble (**polys)[3] = parse_points(str,
			poly_sep, point_sep, coord_sep, NULL, &center);

	GritsPoly *poly = grits_poly_new(polys);
	GRITS_OBJECT(poly)->center = center;
	GRITS_OBJECT(poly)->skip   = GRITS_SKIP_CENTER;
	g_object_weak_ref(G_OBJECT(poly), (GWeakNotify)free_points, polys);
	return poly;
}



/* GObject code */
G_DEFINE_TYPE(GritsPoly, grits_poly, GRITS_TYPE_OBJECT);
static void grits_poly_init(GritsPoly *poly)
{
	poly->border[0] = 1;
	poly->border[1] = 1;
	poly->border[2] = 1;
	poly->border[3] = 0.2;
	poly->width = 1;
	GRITS_OBJECT(poly)->skip = GRITS_SKIP_STATE;;
	poly->iOutlineVbo = 0;
	poly->iOutlineEbo = 0;
	poly->iOutlineVao = 0;
	poly->iTessVbo = 0;
}

static void grits_poly_finalize(GObject *_poly) {
	GritsPoly *poly = GRITS_POLY(_poly);
	glDeleteBuffers(1, &poly->iOutlineVbo);
	glDeleteBuffers(1, &poly->iOutlineEbo);
	glDeleteVertexArrays(1, &poly->iOutlineVao);
	glDeleteBuffers(1, &poly->iTessVbo);
}

static void grits_poly_class_init(GritsPolyClass *klass) {
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->finalize = grits_poly_finalize;
	GritsObjectClass *object_class = GRITS_OBJECT_CLASS(klass);
	objGritsObjectClassPointer = object_class;
	object_class->draw = grits_poly_draw_version_selector;
	object_class->pick = grits_poly_pick;
}
