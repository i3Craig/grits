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

#include <gtk/gtk.h>

/***************************
 * GtkGlExt implementation *
 ***************************/
#if defined(SYS_GTKGLEXT)
#include <gtk/gtkgl.h>
void gtk_gl_enable(GtkWidget *widget)
{
	GdkGLConfig *glconfig = gdk_gl_config_new_by_mode(
			GDK_GL_MODE_RGBA   | GDK_GL_MODE_DEPTH |
			GDK_GL_MODE_ALPHA  | GDK_GL_MODE_DOUBLE);
	gtk_widget_set_gl_capability(widget,
			glconfig, NULL, TRUE, GDK_GL_RGBA_TYPE);
}

void gtk_gl_begin(GtkWidget *widget)
{
	GdkGLContext  *glcontext  = gtk_widget_get_gl_context(widget);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);
	gdk_gl_drawable_gl_begin(gldrawable, glcontext);
}

void gtk_gl_end(GtkWidget *widget)
{
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(widget);
	gdk_gl_drawable_swap_buffers(gldrawable);
	gdk_gl_drawable_gl_end(gldrawable);
}

void gtk_gl_disable(GtkWidget *widget)
{
}


/**********************
 * X11 implementation *
 **********************/
#elif defined(SYS_X11)
#include <GL/glx.h>
#include <gdk/gdkx.h>
static gboolean gtk_gl_errors;
static int gtk_gl_handler(Display *xdisplay, XErrorEvent *xerror)
{
	gtk_gl_errors = TRUE;
	return 0;
}
static GLXContext gtk_gl_create_context(Display *xdisplay, XVisualInfo *xvinfo,
		GLXContext shared, Bool direct)
{
	gtk_gl_errors = FALSE;
	XSync(xdisplay, False);
	void *handler = XSetErrorHandler(gtk_gl_handler);
	GLXContext context = glXCreateContext(xdisplay, xvinfo, shared, direct);
	XSync(xdisplay, False);
	XSetErrorHandler(handler);

	if (gtk_gl_errors)
		return 0;
	return context;
}
void gtk_gl_enable(GtkWidget *widget)
{
	g_debug("GtkGl: enable");
	GdkScreen *screen   = gdk_screen_get_default();
	Display   *xdisplay = GDK_SCREEN_XDISPLAY(screen);
	gint       nscreen  = GDK_SCREEN_XNUMBER(screen);

	/* Create context */
	int attribs[] = {GLX_RGBA,
	                 GLX_RED_SIZE,    1,
	                 GLX_GREEN_SIZE,  1,
	                 GLX_BLUE_SIZE,   1,
	                 GLX_ALPHA_SIZE,  1,
	                 GLX_DOUBLEBUFFER,
	                 GLX_DEPTH_SIZE,  1,
	                 None};

	XVisualInfo *xvinfo  = glXChooseVisual(xdisplay, nscreen, attribs);
	if (!xvinfo)
		g_error("GtkGl: enable - unable to get valid OpenGL Visual");
	GLXContext context = 0;
	if (!context)
		context = gtk_gl_create_context(xdisplay, xvinfo, NULL, True);
	if (!context)
		context = gtk_gl_create_context(xdisplay, xvinfo, NULL, False);
	if (!context)
		g_error("Unable to create OpenGL context,"
		        "possible graphics driver problem?");
	g_debug("GtkGl: direct rendering = %d\n", glXIsDirect(xdisplay, context));

	g_object_set_data(G_OBJECT(widget), "glcontext", context);

	/* Fix up visual/colormap */
#if GTK_CHECK_VERSION(3,0,0)
	GdkVisual *visual = gdk_x11_screen_lookup_visual(screen, xvinfo->visualid);
	gtk_widget_set_visual(widget, visual);
#else
	GdkVisual   *visual = gdk_x11_screen_lookup_visual(screen, xvinfo->visualid);
	GdkColormap *cmap   = gdk_colormap_new(visual, FALSE);
	gtk_widget_set_colormap(widget, cmap);
	g_object_unref(cmap);
#endif
	XFree(xvinfo);

	/* Disable GTK double buffering */
	gtk_widget_set_double_buffered(widget, FALSE);
}

void gtk_gl_begin(GtkWidget *widget)
{
	g_debug("GtkGl: begin");
	Display   *xdisplay = GDK_SCREEN_XDISPLAY(gtk_widget_get_screen(widget));
	Window     xwindow  = GDK_WINDOW_XID(gtk_widget_get_window(widget));
	GLXContext context  = g_object_get_data(G_OBJECT(widget), "glcontext");
	glXMakeCurrent(xdisplay, xwindow, context);
}

void gtk_gl_end(GtkWidget *widget)
{
	g_debug("GtkGl: end");
	Display   *xdisplay = GDK_SCREEN_XDISPLAY(gtk_widget_get_screen(widget));
	Window     xwindow  = GDK_WINDOW_XID(gtk_widget_get_window(widget));
	glXSwapBuffers(xdisplay, xwindow);
}

void gtk_gl_disable(GtkWidget *widget)
{
	g_debug("GtkGl: disable");
	Display   *xdisplay = GDK_SCREEN_XDISPLAY(gtk_widget_get_screen(widget));
	GLXContext context  = g_object_get_data(G_OBJECT(widget), "glcontext");
	glXDestroyContext(xdisplay, context);
}


/************************
 * Win32 implementation *
 ************************/
#elif defined(SYS_WIN)
#include <windows.h>
#include <gdk/gdkwin32.h>
#include <GL/gl.h>

/* Windows doens't define OpenGL extensions */
static void APIENTRY (*glMultiTexCoord2dvPtr)(int target, const double *v);
static void APIENTRY (*glActiveTexturePtr)(int texture);

void APIENTRY glMultiTexCoord2dv(int target, const double *v)
{
	glMultiTexCoord2dvPtr(target, v);
}

void APIENTRY glActiveTexture(int texture)
{
	glActiveTexturePtr(texture);
}

static void init_extensions(void)
{
	static gboolean init_done = FALSE;
	if (init_done)
		return;
	init_done = TRUE;

	g_debug("GtkGl: init_extensions");
	const guchar *exts = NULL;
	if (!(exts = glGetString(GL_EXTENSIONS)))
		g_error("GtkGl: Unable to query extensions");
	if (!(glMultiTexCoord2dvPtr = (void*)wglGetProcAddress("glMultiTexCoord2dvARB")))
		g_error("GtkGl: Unable to load glMultiTexCoord2dv extension:\n%s", exts);
	if (!(glActiveTexturePtr    = (void*)wglGetProcAddress("glActiveTextureARB")))
		g_error("GtkGl: Unable to load glActiveTexture extension\n%s", exts);
	g_debug("GtkGl: extensions - glMultiTexCoord2dvPtr=%p glActiveTexturePtr=%p",
			glMultiTexCoord2dvPtr, glActiveTexturePtr);
}

/* gtkgl implementation */
static void on_realize(GtkWidget *widget, gpointer _)
{
	g_debug("GtkGl: on_realize");
	gdk_window_ensure_native(gtk_widget_get_window(widget));
	gtk_widget_set_double_buffered(widget, FALSE);

	HWND  hwnd = GDK_WINDOW_HWND(gtk_widget_get_window(widget));
	HDC   hDC  = GetDC(hwnd);

	PIXELFORMATDESCRIPTOR pfd = {
		.nSize       = sizeof(pfd),
		.nVersion    = 1,
		.dwFlags     = PFD_DRAW_TO_WINDOW
		             | PFD_SUPPORT_OPENGL
		             | PFD_DOUBLEBUFFER,
		//.dwFlags     = PFD_SUPPORT_OPENGL
		//             | PFD_DRAW_TO_WINDOW,
		.iPixelType  = PFD_TYPE_RGBA,
		.cColorBits  = 24,
		.cAlphaBits  = 8,
		.cDepthBits  = 32,
		.iLayerType  = PFD_MAIN_PLANE,
	};
	int pf = ChoosePixelFormat(hDC, &pfd);
	if (pf == 0)
		g_error("GtkGl: ChoosePixelFormat failed");
	if (!SetPixelFormat(hDC, pf, &pfd))
		g_error("GtkGl: SetPixelFormat failed");
	HGLRC hRC = wglCreateContext(hDC);
	if (hRC == NULL)
		g_error("GtkGl: wglCreateContext failed");
	g_object_set_data(G_OBJECT(widget), "glcontext", hRC);
}

void gtk_gl_enable(GtkWidget *widget)
{
	g_debug("GtkGl: enable");
	g_signal_connect(widget, "realize", G_CALLBACK(on_realize), NULL);
}

void gtk_gl_begin(GtkWidget *widget)
{
	g_debug("GtkGl: begin");
	HWND  hwnd = GDK_WINDOW_HWND(gtk_widget_get_window(widget));
	HDC   hDC  = GetDC(hwnd);
	HGLRC hRC  = g_object_get_data(G_OBJECT(widget), "glcontext");
	if (!wglMakeCurrent(hDC, hRC))
		g_error("GtkGl: wglMakeCurrent failed");
	init_extensions();
}

void gtk_gl_end(GtkWidget *widget)
{
	g_debug("GtkGl: end");
	HWND  hwnd = GDK_WINDOW_HWND(gtk_widget_get_window(widget));
	HDC   hDC  = GetDC(hwnd);
	if (!SwapBuffers(hDC))
		g_error("GtkGl: SwapBuffers failed");
}

void gtk_gl_disable(GtkWidget *widget)
{
	g_debug("GtkGl: disable");
	HGLRC hRC = g_object_get_data(G_OBJECT(widget), "glcontext");
	wglDeleteContext(hRC);
}


/**************************
 * Mac OSX implementation *
 **************************/
#elif defined(SYS_MAC)
#include <gdk/gdkquartz.h>
void gtk_gl_enable(GtkWidget *widget)
{
	g_debug("GtkGl: enable");

	/* Create context */
	NSOpenGLPixelFormatAttribute attribs[] = {
		NSOpenGLPFAColorSize, 24,
		NSOpenGLPFAAlphaSize, 8,
		NSOpenGLPFADepthSize, 1,
		NSOpenGLPFADoubleBuffer,
		0
	};
	NSOpenGLPixelFormat *pix = [[NSOpenGLPixelFormat alloc] initWithAttributes:attribs];
	NSOpenGLContext     *ctx = [[NSOpenGLContext     alloc] initWithFormat:pix shareContext:nil];

	/* Attach to widget */
	gtk_widget_set_double_buffered(widget, FALSE);

	/* Save context */
	g_object_set_data(G_OBJECT(widget), "glcontext", ctx);
}

void gtk_gl_begin(GtkWidget *widget)
{
	g_debug("GtkGl: begin");
	GtkAllocation alloc;
	gdk_window_ensure_native(gtk_widget_get_window(widget));
	gtk_widget_get_allocation(widget, &alloc);
	gtk_widget_translate_coordinates(widget, gtk_widget_get_toplevel(widget),
		0, 0, &alloc.x, &alloc.y);

	NSOpenGLContext *ctx  = g_object_get_data(G_OBJECT(widget), "glcontext");
	GdkWindow       *win  = gtk_widget_get_window(widget);
	NSView          *view = gdk_quartz_window_get_nsview(win);
	NSRect           rect = NSMakeRect(alloc.x, alloc.y, alloc.width, alloc.height);

	[ctx  setView:view];
	[ctx  makeCurrentContext];
	[ctx  update];
	[view setFrame:rect];
	[view setWantsBestResolutionOpenGLSurface:YES];
}

void gtk_gl_end(GtkWidget *widget)
{
	g_debug("GtkGl: end");
	NSOpenGLContext *ctx = g_object_get_data(G_OBJECT(widget), "glcontext");
	[ctx flushBuffer];
}

void gtk_gl_disable(GtkWidget *widget)
{
	g_debug("GtkGl: disable");
}


/****************************
 * Undefined implementation *
 ****************************/
#else
#warning "Unimplemented GtkGl"
void gtk_gl_enable(GtkWidget *widget) { }
void gtk_gl_begin(GtkWidget *widget) { }
void gtk_gl_end(GtkWidget *widget) { }
void gtk_gl_disable(GtkWidget *widget) { }
#endif
