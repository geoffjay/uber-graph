/* uber-graph.c
 *
 * Copyright (C) 2010 Christian Hergert <chris@dronelabs.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "uber-graph.h"
#include "uber-scale.h"

#define WIDGET_CLASS (GTK_WIDGET_CLASS(uber_graph_parent_class))
#define RECT_RIGHT(r)  ((r).x + (r).width)
#define RECT_BOTTOM(r) ((r).y + (r).height)

/**
 * SECTION:uber-graph.h
 * @title: UberGraph
 * @short_description: 
 *
 * Section overview.
 */

G_DEFINE_ABSTRACT_TYPE(UberGraph, uber_graph, GTK_TYPE_DRAWING_AREA)

struct _UberGraphPrivate
{
	cairo_t         *fg_cairo;      /* Cairo context for foreground. */
	cairo_t         *bg_cairo;      /* Cairo context for background. */
	GdkPixmap       *fg_pixmap;     /* Server side pixmap for foreground. */
	GdkPixmap       *bg_pixmap;     /* Server side pixmap for background. */
	GdkRectangle     content_rect;  /* Content area rectangle. */
	GdkRectangle     nonvis_rect;   /* Non-visible drawing area larger than
	                                 * content rect. Used to draw over larger
	                                 * area so we can scroll and not fall out
	                                 * of view.
	                                 */
	UberGraphFormat  format;        /* Data type format. */
	gboolean         paused;        /* Is the graph paused. */
	gboolean         have_rgba;     /* Do we support 32-bit RGBA colormaps. */
	gint             x_slots;       /* Number of data points on x axis. */
	gint             fps;           /* Desired frames per second. */
	gint             fps_real;      /* Milleseconds between FPS callbacks. */
	gfloat           fps_each;      /* How far to move in each FPS tick. */
	guint            fps_handler;   /* Timeout for moving the content. */
	gfloat           dps;           /* Desired data points per second. */
	gint             dps_slot;      /* Which slot in the pixmap buffer. */
	gfloat           dps_each;      /* How many pixels between data points. */
	GTimeVal         dps_tv;        /* Timeval of last data point. */
	guint            dps_handler;   /* Timeout for getting new data. */
	gboolean         fg_dirty;      /* Does the foreground need to be redrawn. */
	gboolean         bg_dirty;      /* Does the background need to be redrawn. */
	guint            tick_len;      /* How long should axis-ticks be. */
	gboolean         show_xlines;   /* Show X axis lines. */
	gboolean         show_xlabels;  /* Show X axis labels. */
	gboolean         show_ylines;   /* Show Y axis lines. */
	gboolean         full_draw;     /* Do we need to redraw all foreground content.
	                                 * If false, draws will try to only add new
	                                 * content to the back buffer.
	                                 */
	GtkWidget       *labels;        /* Container for graph labels. */
	GtkWidget       *align;         /* Alignment for labels. */
	gint             fps_count;     /* Track actual FPS. */
};

static gboolean show_fps = FALSE;

/**
 * uber_graph_new:
 *
 * Creates a new instance of #UberGraph.
 *
 * Returns: the newly created instance of #UberGraph.
 * Side effects: None.
 */
GtkWidget*
uber_graph_new (void)
{
	UberGraph *graph;

	graph = g_object_new(UBER_TYPE_GRAPH, NULL);
	return GTK_WIDGET(graph);
}

/**
 * uber_graph_fps_timeout:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: %TRUE always.
 * Side effects: None.
 */
static gboolean
uber_graph_fps_timeout (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), FALSE);

	priv = graph->priv;
	gtk_widget_queue_draw_area(GTK_WIDGET(graph),
	                           priv->content_rect.x,
	                           priv->content_rect.y,
	                           priv->content_rect.width,
	                           priv->content_rect.height);
	return TRUE;
}

/**
 * uber_graph_get_content_area:
 * @graph: A #UberGraph.
 *
 * Retrieves the content area of the graph.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_get_content_area (UberGraph    *graph, /* IN */
                             GdkRectangle *rect)  /* OUT */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(rect != NULL);

	priv = graph->priv;
	*rect = priv->content_rect;
}

/**
 * uber_graph_get_show_xlines:
 * @graph: A #UberGraph.
 *
 * Retrieves if the X grid lines should be shown.
 *
 * Returns: None.
 * Side effects: None.
 */
gboolean
uber_graph_get_show_xlines (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), FALSE);

	priv = graph->priv;
	return priv->show_xlines;
}

/**
 * uber_graph_set_show_xlines:
 * @graph: A #UberGraph.
 * @show_xlines: Show x lines.
 *
 * Sets if the x lines should be shown.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_show_xlines (UberGraph *graph,       /* IN */
                            gboolean   show_xlines) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->show_xlines = show_xlines;
	priv->bg_dirty = TRUE;
	gtk_widget_queue_draw(GTK_WIDGET(graph));
}

/**
 * uber_graph_get_show_ylines:
 * @graph: A #UberGraph.
 *
 * Retrieves if the X grid lines should be shown.
 *
 * Returns: None.
 * Side effects: None.
 */
gboolean
uber_graph_get_show_ylines (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), FALSE);

	priv = graph->priv;
	return priv->show_ylines;
}

/**
 * uber_graph_set_show_ylines:
 * @graph: A #UberGraph.
 * @show_ylines: Show x lines.
 *
 * Sets if the x lines should be shown.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_show_ylines (UberGraph *graph,       /* IN */
                            gboolean   show_ylines) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->show_ylines = show_ylines;
	priv->bg_dirty = TRUE;
	gtk_widget_queue_draw(GTK_WIDGET(graph));
}

/**
 * uber_graph_get_labels:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
GtkWidget*
uber_graph_get_labels (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), NULL);

	priv = graph->priv;
	return priv->align;
}

/**
 * uber_graph_get_next_data:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static inline gboolean
uber_graph_get_next_data (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	gboolean ret = TRUE;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), FALSE);

	/*
	 * Get the current time for this data point.  This is used to calculate
	 * the proper offset in the FPS callback.
	 */
	priv = graph->priv;
	g_get_current_time(&priv->dps_tv);
	/*
	 * Notify the subclass to retrieve the data point.
	 */
	if (UBER_GRAPH_GET_CLASS(graph)->get_next_data) {
		ret = UBER_GRAPH_GET_CLASS(graph)->get_next_data(graph);
	}
	return ret;
}

/**
 * uber_graph_init_texture:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_init_texture (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	GdkDrawable *drawable;
	GdkColormap *colormap;
	GdkVisual *visual;
	gint depth = -1;
	gint width;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	/*
	 * Get drawable to base pixmaps upon.
	 */
	if (!(drawable = gtk_widget_get_window(GTK_WIDGET(graph)))) {
		g_critical("%s() called before GdkWindow is allocated.", G_STRFUNC);
		return;
	}
	/*
	 * Check if we can do 32-bit RGBA colormaps.
	 */
	if (priv->have_rgba) {
		drawable = NULL;
		depth = 32;
	}
	/*
	 * Initialize foreground and background pixmaps.
	 */
	width = MAX(priv->nonvis_rect.x + priv->nonvis_rect.width, alloc.width);
	priv->fg_pixmap = gdk_pixmap_new(drawable, width, alloc.height, depth);
	/*
	 * Create a 32-bit colormap if needed.
	 */
	if (priv->have_rgba) {
		visual = gdk_visual_get_best_with_depth(depth);
		colormap = gdk_colormap_new(visual, FALSE);
		gdk_drawable_set_colormap(GDK_DRAWABLE(priv->fg_pixmap), colormap);
		g_object_unref(colormap);
	}
	/*
	 * Create cairo textures for drawing.
	 */
	priv->fg_cairo = gdk_cairo_create(priv->fg_pixmap);
}

/**
 * uber_graph_destroy_bg:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_destroy_bg (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	if (priv->bg_cairo) {
		cairo_destroy(priv->bg_cairo);
		priv->bg_cairo = NULL;
	}
	if (priv->bg_pixmap) {
		g_object_unref(priv->bg_pixmap);
		priv->bg_pixmap = NULL;
	}
}

/**
 * uber_graph_init_bg:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_init_bg (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	GdkDrawable *drawable;
	GtkAllocation alloc;
	GdkVisual *visual;
	GdkColormap *colormap;
	gint depth = 32;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	/*
	 * Get drawable for pixmap.
	 */
	if (!(drawable = gtk_widget_get_window(GTK_WIDGET(graph)))) {
		g_critical("%s() called before GdkWindow is allocated.", G_STRFUNC);
		return;
	}
	/*
	 * Fallback if we don't have 32-bit RGBA rendering.
	 */
	if (!priv->have_rgba) {
		depth = -1;
	}
	/*
	 * Create the server-side pixmap.
	 */
	priv->bg_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, depth);
	/*
	 * Setup 32-bit colormap if needed.
	 */
	if (priv->have_rgba) {
		visual = gdk_visual_get_best_with_depth(depth);
		colormap = gdk_colormap_new(visual, FALSE);
		gdk_drawable_set_colormap(GDK_DRAWABLE(priv->bg_pixmap), colormap);
		g_object_unref(colormap);
	}
	/*
	 * Create cairo texture for drawing.
	 */
	priv->bg_cairo = gdk_cairo_create(priv->bg_pixmap);
}

/**
 * uber_graph_destroy_texture:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_destroy_texture (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	if (priv->fg_cairo) {
		cairo_destroy(priv->fg_cairo);
		priv->fg_cairo = NULL;
	}
	if (priv->fg_pixmap) {
		g_object_unref(priv->fg_pixmap);
		priv->fg_pixmap = NULL;
	}
}

/**
 * uber_graph_calculate_rects:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_calculate_rects (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	PangoLayout *layout;
	PangoFontDescription *font_desc;
	GdkDrawable *drawable;
	gint pango_width;
	gint pango_height;
	cairo_t *cr;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	/*
	 * We can't calculate rectangles before we have a GdkWindow.
	 */
	if (!(drawable = gtk_widget_get_window(GTK_WIDGET(graph)))) {
		return;
	}
	/*
	 * Determine the pixels required for labels.
	 */
	cr = gdk_cairo_create(drawable);
	layout = pango_cairo_create_layout(cr);
	font_desc = pango_font_description_new();
	pango_font_description_set_family_static(font_desc, "Monospace");
	pango_font_description_set_size(font_desc, 6 * PANGO_SCALE);
	pango_layout_set_font_description(layout, font_desc);
	pango_layout_set_text(layout, "XXXXXXXXXX", -1);
	pango_layout_get_pixel_size(layout, &pango_width, &pango_height);
	pango_font_description_free(font_desc);
	g_object_unref(layout);
	cairo_destroy(cr);
	/*
	 * Calculate content area rectangle.
	 */
	priv->content_rect.x = priv->tick_len + pango_width + 1.5;
	priv->content_rect.y = (pango_height / 2.) + 1.5;
	priv->content_rect.width = alloc.width - priv->content_rect.x - 3.0;
	priv->content_rect.height = alloc.height - priv->tick_len - pango_height
	                          - (pango_height / 2.) - 3.0;
	if (!priv->show_xlabels) {
		priv->content_rect.height += pango_height;
	}
	/*
	 * Adjust label offset.
	 */
	/*
	 * Calculate FPS/DPS adjustments.
	 */
	priv->dps_each = ceil((gfloat)priv->content_rect.width
	                      / (gfloat)(priv->x_slots - 1));
	priv->fps_each = priv->dps_each
	               / ((gfloat)priv->fps / (gfloat)priv->dps);
	/*
	 * XXX: Small hack to make things a bit smoother at small scales.
	 */
	if (priv->fps_each < .5) {
		priv->fps_each = 1;
		priv->fps_real = (1000. / priv->dps_each) / 2.;
	} else {
		priv->fps_real = 1000. / priv->fps;
	}
	/*
	 * Update FPS callback.
	 */
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
		priv->fps_handler = g_timeout_add(priv->fps_real,
		                                  (GSourceFunc)uber_graph_fps_timeout,
		                                  graph);
	}
	/*
	 * Calculate the non-visible area that drawing should happen within.
	 */
	priv->nonvis_rect = priv->content_rect;
	priv->nonvis_rect.width = priv->dps_each * priv->x_slots;
	/*
	 * Update positioning for label alignment.
	 */
	gtk_alignment_set_padding(GTK_ALIGNMENT(priv->align),
	                          6, 6, priv->content_rect.x, 0);
}

/**
 * uber_graph_get_show_xlabels:
 * @graph: A #UberGraph.
 *
 * Retrieves if the X grid labels should be shown.
 *
 * Returns: None.
 * Side effects: None.
 */
gboolean
uber_graph_get_show_xlabels (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), FALSE);

	priv = graph->priv;
	return priv->show_xlabels;
}

/**
 * uber_graph_set_show_xlabels:
 * @graph: A #UberGraph.
 * @show_xlabels: Show x labels.
 *
 * Sets if the x labels should be shown.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_show_xlabels (UberGraph *graph,       /* IN */
                             gboolean   show_xlabels) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->show_xlabels = show_xlabels;
	priv->bg_dirty = TRUE;
	uber_graph_calculate_rects(graph);
	gtk_widget_queue_draw(GTK_WIDGET(graph));
}

/**
 * uber_graph_dps_timeout:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: %TRUE always.
 * Side effects: None.
 */
static gboolean
uber_graph_dps_timeout (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), FALSE);

	priv = graph->priv;
	if (!uber_graph_get_next_data(graph)) {
		/*
		 * XXX: How should we handle failed data retrieval.
		 */
	}
	if (G_UNLIKELY(show_fps)) {
		g_print("UberGraph[%p] %02d FPS\n", graph, priv->fps_count);
	}
	priv->fps_count = 0;
	/*
	 * Make sure the content is re-rendered.
	 */
	priv->fg_dirty = TRUE;
	if (!priv->paused) {
		gtk_widget_queue_draw_area(GTK_WIDGET(graph),
		                           priv->content_rect.x,
		                           priv->content_rect.y,
		                           priv->content_rect.width,
		                           priv->content_rect.height);
	}
	return TRUE;
}

/**
 * uber_graph_register_dps_handler:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_register_dps_handler (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	guint dps_freq;
	gboolean do_now = TRUE;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	if (priv->dps_handler) {
		g_source_remove(priv->dps_handler);
		do_now = FALSE;
	}
	/*
	 * Calculate the update frequency.
	 */
	dps_freq = 1000 / priv->dps;
	/*
	 * Install the data handler.
	 */
	priv->dps_handler = g_timeout_add(dps_freq,
	                                  (GSourceFunc)uber_graph_dps_timeout,
	                                  graph);
	/*
	 * Call immediately.
	 */
	if (do_now) {
		uber_graph_dps_timeout(graph);
	}
}

/**
 * uber_graph_register_fps_handler:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_register_fps_handler (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	/*
	 * Remove any existing FPS handler.
	 */
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
	}
	/*
	 * Install the FPS timeout.
	 */
	priv->fps_handler = g_timeout_add(priv->fps_real,
	                                  (GSourceFunc)uber_graph_fps_timeout,
	                                  graph);
}

/**
 * uber_graph_set_dps:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_dps (UberGraph *graph, /* IN */
                    gfloat     dps)   /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->dps = dps;
	/*
	 * TODO: Does this belong somewhere else?
	 */
	if (UBER_GRAPH_GET_CLASS(graph)->set_stride) {
		UBER_GRAPH_GET_CLASS(graph)->set_stride(graph, priv->x_slots);
	}
	/*
	 * Recalculate frame rates and timeouts.
	 */
	uber_graph_calculate_rects(graph);
	uber_graph_register_dps_handler(graph);
	uber_graph_register_fps_handler(graph);
}

/**
 * uber_graph_set_fps:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_fps (UberGraph *graph, /* IN */
                    guint      fps)   /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->fps = fps;
	uber_graph_register_fps_handler(graph);
}

/**
 * uber_graph_realize:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_realize (GtkWidget *widget) /* IN */
{
	UberGraph *graph;
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	graph = UBER_GRAPH(widget);
	priv = graph->priv;
	WIDGET_CLASS->realize(widget);
	/*
	 * Calculate new layout based on allocation.
	 */
	uber_graph_calculate_rects(graph);
	/*
	 * Re-initialize textures for updated sizes.
	 */
	uber_graph_destroy_bg(graph);
	uber_graph_destroy_texture(graph);
	uber_graph_init_bg(graph);
	uber_graph_init_texture(graph);
	/*
	 * Notify subclass of current data stride (points per graph).
	 */
	if (UBER_GRAPH_GET_CLASS(widget)->set_stride) {
		UBER_GRAPH_GET_CLASS(widget)->set_stride(UBER_GRAPH(widget),
		                                         priv->x_slots);
	}
	/*
	 * Install the data collector.
	 */
	uber_graph_register_dps_handler(graph);
}

/**
 * uber_graph_unrealize:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_unrealize (GtkWidget *widget) /* IN */
{
	UberGraph *graph;
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	graph = UBER_GRAPH(widget);
	priv = graph->priv;
	/*
	 * Unregister any data acquisition handlers.
	 */
	if (priv->dps_handler) {
		g_source_remove(priv->dps_handler);
		priv->dps_handler = 0;
	}
	/*
	 * Destroy textures.
	 */
	uber_graph_destroy_bg(graph);
	uber_graph_destroy_texture(graph);
}

/**
 * uber_graph_screen_changed:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_screen_changed (GtkWidget *widget,     /* IN */
                           GdkScreen *old_screen) /* IN */
{
	UberGraphPrivate *priv;
	GdkScreen *screen;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	priv = UBER_GRAPH(widget)->priv;
	/*
	 * Check if we have RGBA colormaps available.
	 */
	priv->have_rgba = FALSE;
	if ((screen = gtk_widget_get_screen(widget))) {
		priv->have_rgba = !!gdk_screen_get_rgba_colormap(screen);
	}
}

/**
 * uber_graph_show:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_show (GtkWidget *widget) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	priv = UBER_GRAPH(widget)->priv;
	WIDGET_CLASS->show(widget);
	/*
	 * Only run the FPS timeout when we are visible.
	 */
	uber_graph_register_fps_handler(UBER_GRAPH(widget));
}

/**
 * uber_graph_hide:
 * @widget: A #GtkWIdget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_hide (GtkWidget *widget) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	priv = UBER_GRAPH(widget)->priv;
	/*
	 * Disable the FPS timeout when we are not visible.
	 */
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
		priv->fps_handler = 0;
	}
}

static inline void
uber_graph_get_pixmap_rect (UberGraph    *graph, /* IN */
                            GdkRectangle *rect)  /* OUT */
{
	UberGraphPrivate *priv;
	GtkAllocation alloc;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	rect->x = 0;
	rect->y = 0;
	rect->width = MAX(alloc.width,
	                  priv->nonvis_rect.x + priv->nonvis_rect.width);
	rect->height = alloc.height;
}

/**
 * uber_graph_render_fg:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_render_fg (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	GdkRectangle rect;
	gfloat each;
	gfloat x_epoch;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	/*
	 * Acquire resources.
	 */
	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	uber_graph_get_pixmap_rect(graph, &rect);
	/*
	 * Render to texture if needed.
	 */
	if (priv->fg_dirty) {
		/*
		 * Caclulate relative positionings for use in renderers.
		 */
		each = priv->content_rect.width / (gfloat)(priv->x_slots - 1);
		x_epoch = RECT_RIGHT(priv->nonvis_rect);
		/*
		 * If we are in a fast draw, lets copy the content from the other
		 * buffer at the next offset.
		 */
		if (!priv->full_draw && UBER_GRAPH_GET_CLASS(graph)->render_fast) {
			/*
			 * Determine next rendering slot.
			 */
			rect.x = priv->content_rect.x
			       + (priv->dps_each * priv->dps_slot);
			rect.width = priv->dps_each;
			rect.y = priv->content_rect.y;
			rect.height = priv->content_rect.height;
			priv->dps_slot = (priv->dps_slot + 1) % priv->x_slots;
			x_epoch = RECT_RIGHT(rect);
			/*
			 * Clear content area.
			 */
			cairo_save(priv->fg_cairo);
			cairo_set_operator(priv->fg_cairo, CAIRO_OPERATOR_CLEAR);
			gdk_cairo_rectangle(priv->fg_cairo, &rect);
			cairo_fill(priv->fg_cairo);
			cairo_restore(priv->fg_cairo);

#if 0
			/*
			 * XXX: Draw line helper for debugging.
			 */
			cairo_save(priv->fg_cairo);
			cairo_set_source_rgb(priv->fg_cairo, .3, .3, .3);
			cairo_rectangle(priv->fg_cairo,
			                rect.x,
			                rect.y + (rect.height / (gfloat)priv->x_slots * priv->dps_slot),
			                rect.width,
			                rect.height / priv->x_slots);
			cairo_fill(priv->fg_cairo);
			cairo_restore(priv->fg_cairo);
#endif

			/*
			 * Render new content clipped.
			 */
			cairo_save(priv->fg_cairo);
			gdk_cairo_reset_clip(priv->fg_cairo, priv->fg_pixmap);
			gdk_cairo_rectangle(priv->fg_cairo, &rect);
			cairo_clip(priv->fg_cairo);
			/*
			 * Determine area for this draw.
			 */
			UBER_GRAPH_GET_CLASS(graph)->render_fast(graph,
			                                         priv->fg_cairo,
			                                         &rect,
			                                         x_epoch,
			                                         each + .5);
			cairo_restore(priv->fg_cairo);
		} else {
			/*
			 * Clear content area.
			 */
			cairo_save(priv->fg_cairo);
			cairo_set_operator(priv->fg_cairo, CAIRO_OPERATOR_CLEAR);
			gdk_cairo_rectangle(priv->fg_cairo, &rect);
			cairo_fill(priv->fg_cairo);
			cairo_restore(priv->fg_cairo);
			/*
			 * Draw the entire foreground.
			 */
			if (UBER_GRAPH_GET_CLASS(graph)->render) {
				priv->dps_slot = 0;
				cairo_save(priv->fg_cairo);
				gdk_cairo_rectangle(priv->fg_cairo, &priv->nonvis_rect);
				cairo_clip(priv->fg_cairo);
				UBER_GRAPH_GET_CLASS(graph)->render(graph,
				                                    priv->fg_cairo,
				                                    &priv->nonvis_rect,
				                                    x_epoch,
				                                    each);
				cairo_restore(priv->fg_cairo);
			}
		}
	}
	/*
	 * Foreground is no longer dirty.
	 */
	priv->fg_dirty = FALSE;
	priv->full_draw = FALSE;
}

/**
 * uber_graph_redraw:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_redraw (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->fg_dirty = TRUE;
	priv->bg_dirty = TRUE;
	priv->full_draw = TRUE;
	gtk_widget_queue_draw(GTK_WIDGET(graph));
}

/**
 * uber_graph_get_yrange:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static inline void
uber_graph_get_yrange (UberGraph *graph, /* IN */
                       UberRange *range) /* OUT */
{
	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(range != NULL);

	memset(range, 0, sizeof(*range));
	if (UBER_GRAPH_GET_CLASS(graph)->get_yrange) {
		UBER_GRAPH_GET_CLASS(graph)->get_yrange(graph, range);
	}
}

/**
 * uber_graph_set_format:
 * @graph: A UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_format (UberGraph       *graph, /* IN */
                       UberGraphFormat  format) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->format = format;
	priv->bg_dirty = TRUE;
	gtk_widget_queue_draw(GTK_WIDGET(graph));
}

static void
uber_graph_render_x_axis (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	const gdouble dashes[] = { 1.0, 2.0 };
	PangoFontDescription *fd;
	PangoLayout *pl;
	GtkStyle *style;
	gfloat each;
	gfloat x;
	gfloat y;
	gfloat h;
	gchar text[16] = { 0 };
	gint count;
	gint wi;
	gint hi;
	gint i;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	style = gtk_widget_get_style(GTK_WIDGET(graph));
	count = priv->x_slots / 10;
	each = priv->content_rect.width / (gfloat)count;
	/*
	 * Draw ticks.
	 */
	cairo_save(priv->bg_cairo);
	pl = pango_cairo_create_layout(priv->bg_cairo);
	fd = pango_font_description_new();
	pango_font_description_set_family_static(fd, "Monospace");
	pango_font_description_set_size(fd, 6 * PANGO_SCALE);
	pango_layout_set_font_description(pl, fd);
	gdk_cairo_set_source_color(priv->bg_cairo, &style->fg[GTK_STATE_NORMAL]);
	cairo_set_line_width(priv->bg_cairo, 1.0);
	cairo_set_dash(priv->bg_cairo, dashes, G_N_ELEMENTS(dashes), 0);
	for (i = 0; i <= count; i++) {
		x = RECT_RIGHT(priv->content_rect) - (gint)(i * each) + .5;
		if (priv->show_xlines && (i != 0 && i != count)) {
			y = priv->content_rect.y;
			h = priv->content_rect.height + priv->tick_len;
		} else {
			y = priv->content_rect.y + priv->content_rect.height;
			h = priv->tick_len;
		}
		if (i != 0 && i != count) {
			cairo_move_to(priv->bg_cairo, x, y);
			cairo_line_to(priv->bg_cairo, x, y + h);
			cairo_stroke(priv->bg_cairo);
		}
		/*
		 * Render the label.
		 */
		if (priv->show_xlabels) {
			g_snprintf(text, sizeof(text), "%d", i * 10);
			pango_layout_set_text(pl, text, -1);
			pango_layout_get_pixel_size(pl, &wi, &hi);
			if (i != 0 && i != count) {
				cairo_move_to(priv->bg_cairo, x - (wi / 2), y + h);
			} else if (i == 0) {
				cairo_move_to(priv->bg_cairo,
				              RECT_RIGHT(priv->content_rect) - (wi / 2),
				              RECT_BOTTOM(priv->content_rect) + priv->tick_len);
			} else if (i == count) {
				cairo_move_to(priv->bg_cairo,
				              priv->content_rect.x - (wi / 2),
				              RECT_BOTTOM(priv->content_rect) + priv->tick_len);
			}
			pango_cairo_show_layout(priv->bg_cairo, pl);
		}
	}
	g_object_unref(pl);
	pango_font_description_free(fd);
	cairo_restore(priv->bg_cairo);
}

static inline void G_GNUC_PRINTF(6, 7)
uber_graph_render_y_line (UberGraph   *graph,     /* IN */
                          cairo_t     *cr,        /* IN */
                          gint         y,         /* IN */
                          gboolean     tick_only, /* IN */
                          gboolean     no_label,  /* IN */
                          const gchar *format,    /* IN */
                          ...)                    /* IN */
{
	UberGraphPrivate *priv;
	const gdouble dashes[] = { 1.0, 2.0 };
	PangoFontDescription *fd;
	PangoLayout *pl;
	GtkStyle *style;
	va_list args;
	gchar *text;
	gint width;
	gint height;
	gfloat real_y = y + .5;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(cr != NULL);
	g_return_if_fail(format != NULL);

	priv = graph->priv;
	style = gtk_widget_get_style(GTK_WIDGET(graph));
	/*
	 * Draw grid line.
	 */
	cairo_save(cr);
	cairo_set_dash(cr, dashes, G_N_ELEMENTS(dashes), 0);
	cairo_set_line_width(cr, 1.0);
	gdk_cairo_set_source_color(cr, &style->fg[GTK_STATE_NORMAL]);
	cairo_move_to(cr, priv->content_rect.x - priv->tick_len, real_y);
	if (tick_only) {
		cairo_line_to(cr, priv->content_rect.x, real_y);
	} else {
		cairo_line_to(cr, RECT_RIGHT(priv->content_rect), real_y);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
	/*
	 * Show label.
	 */
	if (!no_label) {
		cairo_save(cr);
		gdk_cairo_set_source_color(cr, &style->fg[GTK_STATE_NORMAL]);
		/*
		 * Format text.
		 */
		va_start(args, format);
		text = g_strdup_vprintf(format, args);
		va_end(args);
		/*
		 * Render pango layout.
		 */
		pl = pango_cairo_create_layout(cr);
		fd = pango_font_description_new();
		pango_font_description_set_family_static(fd, "Monospace");
		pango_font_description_set_size(fd, 6 * PANGO_SCALE);
		pango_layout_set_font_description(pl, fd);
		pango_layout_set_text(pl, text, -1);
		pango_layout_get_pixel_size(pl, &width, &height);
		cairo_move_to(cr, priv->content_rect.x - priv->tick_len - width - 3,
		              real_y - height / 2);
		pango_cairo_show_layout(cr, pl);
		/*
		 * Cleanup resources.
		 */
		g_free(text);
		pango_font_description_free(fd);
		g_object_unref(pl);
		cairo_restore(cr);
	}
}

static void
uber_graph_render_y_axis_percent (UberGraph *graph,       /* IN */
                                  UberRange *range,       /* IN */
                                  UberRange *pixel_range, /* IN */
                                  gint       n_lines)     /* IN */
{
	static const gchar format[] = "%0.0f %%";
	UberGraphPrivate *priv;
	gdouble value;
	gdouble y;
	gint i;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(range != NULL);
	g_return_if_fail(pixel_range != NULL);
	g_return_if_fail(n_lines >= 0);
	g_return_if_fail(n_lines < 6);

	priv = graph->priv;
	/*
	 * Render top and bottom lines.
	 */
	uber_graph_render_y_line(graph, priv->bg_cairo,
	                         priv->content_rect.y - 1,
	                         TRUE, FALSE, format, 100.);
	uber_graph_render_y_line(graph, priv->bg_cairo,
	                         RECT_BOTTOM(priv->content_rect),
	                         TRUE, FALSE, format, 0.);
	/*
	 * Render lines between the edges.
	 */
	for (i = 1; i < n_lines; i++) {
		value = (n_lines - i) / (gfloat)n_lines;
		y = pixel_range->end - (pixel_range->range * value);
		value *= 100.;
		uber_graph_render_y_line(graph, priv->bg_cairo, y,
		                         !priv->show_ylines, FALSE,
		                         format, value);
	}
}

/**
 * uber_graph_render_y_axis_direct:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_render_y_axis_direct (UberGraph *graph,       /* IN */
                                 UberRange *range,       /* IN */
                                 UberRange *pixel_range, /* IN */
                                 gint       n_lines,     /* IN */
                                 gboolean   kibi)        /* IN */
{
	static const gchar format[] = "%0.1f%s";
	const gchar *modifier = "";
	UberGraphPrivate *priv;
	gdouble value;
	gdouble y;
	gint i;

	g_return_if_fail(UBER_IS_GRAPH(graph));

#define CONDENSE(v) \
G_STMT_START { \
    if (kibi) { \
        if ((v) >= 1073741824.) { \
            (v) /= 1073741824.; \
            modifier = " Gi"; \
        } else if ((v) >= 1048576.) { \
            (v) /= 1048576.; \
            modifier = " Mi"; \
        } else if ((v) >= 1024.) { \
            (v) /= 1024.; \
            modifier = " Ki"; \
        } else { \
            modifier = ""; \
        } \
    } else {  \
        if ((v) >= 1000000000.) { \
            (v) /= 1000000000.; \
            modifier = " G"; \
        } else if ((v) >= 1000000.) { \
            (v) /= 1000000.; \
            modifier = " M"; \
        } else if ((v) >= 1000.) { \
            (v) /= 1000.; \
            modifier = " K"; \
        } else { \
            modifier = ""; \
        } \
    } \
} G_STMT_END

	priv = graph->priv;
	/*
	 * Render top and bottom lines.
	 */
	value = range->end;
	CONDENSE(value);
	uber_graph_render_y_line(graph, priv->bg_cairo,
	                         priv->content_rect.y - 1,
	                         TRUE, FALSE, format, value, modifier);
	value = range->begin;
	CONDENSE(value);
	uber_graph_render_y_line(graph, priv->bg_cairo,
	                         RECT_BOTTOM(priv->content_rect),
	                         TRUE, FALSE, format, value, modifier);
	/*
	 * Render lines between the edges.
	 */
	for (i = 1; i < n_lines; i++) {
		y = value = range->range / (gfloat)(n_lines) * (gfloat)i;
		/*
		 * TODO: Use proper scale.
		 */
		uber_scale_linear(range, pixel_range, &y, NULL);
		if (y == 0 || y == pixel_range->begin || y == pixel_range->end) {
			continue;
		}
		y = pixel_range->end - y;
		CONDENSE(value);
		uber_graph_render_y_line(graph, priv->bg_cairo, y,
		                         !priv->show_ylines,
		                         (range->begin == range->end),
		                         format, value, modifier);
	}
}

/**
 * uber_graph_render_y_axis:
 * @graph: A #UberGraph.
 *
 * Render the Y axis grid lines and labels.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_render_y_axis (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	UberRange pixel_range;
	UberRange range;
	gint n_lines;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	/*
	 * Calculate ranges.
	 */
	uber_graph_get_yrange(graph, &range);
	pixel_range.begin = priv->content_rect.y;
	pixel_range.end = priv->content_rect.y + priv->content_rect.height;
	pixel_range.range = pixel_range.end - pixel_range.begin;
	/*
	 * Render grid lines for given format.
	 */
	n_lines = MIN(priv->content_rect.height / 25, 5);
	switch (priv->format) {
	case UBER_GRAPH_FORMAT_PERCENT:
		uber_graph_render_y_axis_percent(graph, &range, &pixel_range, n_lines);
		break;
	case UBER_GRAPH_FORMAT_DIRECT:
		uber_graph_render_y_axis_direct(graph, &range, &pixel_range,
		                                n_lines, FALSE);
		break;
	case UBER_GRAPH_FORMAT_DIRECT1024:
		uber_graph_render_y_axis_direct(graph, &range, &pixel_range,
		                                n_lines, TRUE);
		break;
	default:
		g_assert_not_reached();
	}
}

/**
 * uber_graph_render_bg:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_render_bg (UberGraph *graph) /* IN */
{
	const gdouble dashes[] = { 1.0, 2.0 };
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	GtkStyle *style;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	/*
	 * Acquire resources.
	 */
	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	style = gtk_widget_get_style(GTK_WIDGET(graph));
	/*
	 * Ensure valid resources.
	 */
	g_assert(style);
	g_assert(priv->bg_cairo);
	g_assert(priv->bg_pixmap);
	/*
	 * Clear entire background.  Hopefully this looks okay for RGBA themes
	 * that are translucent.
	 */
	cairo_save(priv->bg_cairo);
	cairo_set_operator(priv->bg_cairo, CAIRO_OPERATOR_CLEAR);
	cairo_rectangle(priv->bg_cairo, 0, 0, alloc.width, alloc.height);
	cairo_fill(priv->bg_cairo);
	cairo_restore(priv->bg_cairo);
	/*
	 * Paint the content area background.
	 */
	cairo_save(priv->bg_cairo);
	gdk_cairo_set_source_color(priv->bg_cairo, &style->light[GTK_STATE_NORMAL]);
	gdk_cairo_rectangle(priv->bg_cairo, &priv->content_rect);
	cairo_fill(priv->bg_cairo);
	cairo_restore(priv->bg_cairo);
	/*
	 * Stroke the border around the content area.
	 */
	cairo_save(priv->bg_cairo);
	gdk_cairo_set_source_color(priv->bg_cairo, &style->fg[GTK_STATE_NORMAL]);
	cairo_set_line_width(priv->bg_cairo, 1.0);
	cairo_set_dash(priv->bg_cairo, dashes, G_N_ELEMENTS(dashes), 0);
	cairo_set_antialias(priv->bg_cairo, CAIRO_ANTIALIAS_NONE);
	cairo_rectangle(priv->bg_cairo,
	                priv->content_rect.x - .5,
	                priv->content_rect.y - .5,
	                priv->content_rect.width + 1.0,
	                priv->content_rect.height + 1.0);
	cairo_stroke(priv->bg_cairo);
	cairo_restore(priv->bg_cairo);
	/*
	 * Render the axis ticks.
	 */
	uber_graph_render_y_axis(graph);
	uber_graph_render_x_axis(graph);
	/*
	 * Background is no longer dirty.
	 */
	priv->bg_dirty = FALSE;
}

static inline void
g_time_val_subtract (GTimeVal *a, /* IN */
                     GTimeVal *b, /* IN */
                     GTimeVal *c) /* OUT */
{
	g_return_if_fail(a != NULL);
	g_return_if_fail(b != NULL);
	g_return_if_fail(c != NULL);

	c->tv_sec = a->tv_sec - b->tv_sec;
	c->tv_usec = a->tv_usec - b->tv_usec;
	if (c->tv_usec < 0) {
		c->tv_usec += G_USEC_PER_SEC;
		c->tv_sec -= 1;
	}
}

/**
 * uber_graph_get_fps_offset:
 * @graph: A #UberGraph.
 *
 * Calculates the number of pixels that the foreground should be rendered
 * from the origin.
 *
 * Returns: The pixel offset to render the foreground.
 * Side effects: None.
 */
static gfloat
uber_graph_get_fps_offset (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	GTimeVal rel;
	GTimeVal tv;
	gfloat f;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), 0.);

	priv = graph->priv;
	g_get_current_time(&tv);
	g_time_val_subtract(&tv, &priv->dps_tv, &rel);
	f = ((rel.tv_sec * 1000) + (rel.tv_usec / 1000))
	  / (1000. / priv->dps) /* MSec Per Data Point */
	  * priv->dps_each;     /* Pixels Per Data Point */
	return MIN(f, (priv->dps_each - priv->fps_each));
}

/**
 * uber_graph_expose_event:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: %FALSE always.
 * Side effects: None.
 */
static gboolean
uber_graph_expose_event (GtkWidget      *widget, /* IN */
                         GdkEventExpose *expose) /* IN */
{
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	cairo_t *cr;
	gfloat offset;
	gint x;

	g_return_val_if_fail(UBER_IS_GRAPH(widget), FALSE);

	priv = UBER_GRAPH(widget)->priv;
	gtk_widget_get_allocation(widget, &alloc);
	priv->fps_count++;
	/*
	 * Ensure that the texture is initialized.
	 */
	g_assert(priv->fg_pixmap);
	g_assert(priv->bg_pixmap);
	/*
	 * Clear window background.
	 */
	gdk_window_clear_area(expose->window,
	                      expose->area.x,
	                      expose->area.y,
	                      expose->area.width,
	                      expose->area.height);
	/*
	 * Allocate resources.
	 */
	cr = gdk_cairo_create(expose->window);
	/*
	 * Clip to exposure area.
	 */
	gdk_cairo_rectangle(cr, &expose->area);
	cairo_clip(cr);
	/*
	 * Render background or foreground if needed.
	 */
	if (priv->bg_dirty) {
		uber_graph_render_bg(UBER_GRAPH(widget));
	}
	if (priv->fg_dirty) {
		uber_graph_render_fg(UBER_GRAPH(widget));
	}
	/*
	 * Paint the background to the exposure area.
	 */
	cairo_save(cr);
	gdk_cairo_set_source_pixmap(cr, priv->bg_pixmap, 0, 0);
	cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
	cairo_paint(cr);
	cairo_restore(cr);
	/*
	 * Draw the foreground.
	 */
	offset = uber_graph_get_fps_offset(UBER_GRAPH(widget));
	if (priv->have_rgba) {
		cairo_save(cr);
		/*
		 * Clip exposure to the content area.
		 */
		gdk_cairo_reset_clip(cr, expose->window);
		gdk_cairo_rectangle(cr, &priv->content_rect);
		cairo_clip(cr);

		/*
		 * Data in the fg pixmap is a ring bufer. Render the first portion
		 * at its given offset.
		 */
		x = ((priv->x_slots - priv->dps_slot) * priv->dps_each) - offset;
		gdk_cairo_set_source_pixmap(cr, priv->fg_pixmap, (gint)x, 0);
		gdk_cairo_rectangle(cr, &priv->content_rect);
		cairo_paint(cr);

		/*
		 * Render the second part of the ring pixmap buffer.
		 */
		x = (priv->dps_each * -priv->dps_slot) - offset;
		gdk_cairo_set_source_pixmap(cr, priv->fg_pixmap, (gint)x, 0);
		gdk_cairo_rectangle(cr, &priv->content_rect);
		cairo_paint(cr);

		cairo_restore(cr);
	} else {
		/*
		 * TODO: Use XOR command for fallback.
		 */
		g_warn_if_reached();
	}
	/*
	 * Cleanup resources.
	 */
	cairo_destroy(cr);
	return FALSE;
}

/**
 * uber_graph_style_set:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_style_set (GtkWidget *widget,    /* IN */
                      GtkStyle  *old_style) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	priv = UBER_GRAPH(widget)->priv;
	WIDGET_CLASS->style_set(widget, old_style);
	priv->fg_dirty = TRUE;
	priv->bg_dirty = TRUE;
	gtk_widget_queue_draw(widget);
}

/**
 * uber_graph_size_allocate:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_size_allocate (GtkWidget     *widget, /* IN */
                          GtkAllocation *alloc)  /* IN */
{
	UberGraph *graph;
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	graph = UBER_GRAPH(widget);
	priv = graph->priv;
	WIDGET_CLASS->size_allocate(widget, alloc);
	/*
	 * If there is no window yet, we can defer setup.
	 */
	if (!gtk_widget_get_window(widget)) {
		return;
	}
	/*
	 * Recalculate rectangles.
	 */
	uber_graph_calculate_rects(graph);
	/*
	 * Recreate server side pixmaps.
	 */
	uber_graph_destroy_bg(graph);
	uber_graph_destroy_texture(graph);
	uber_graph_init_bg(graph);
	uber_graph_init_texture(graph);
	/*
	 * Mark foreground and background as dirty.
	 */
	priv->fg_dirty = TRUE;
	priv->bg_dirty = TRUE;
	priv->full_draw = TRUE;
	gtk_widget_queue_draw(widget);
}

static void
uber_graph_size_request (GtkWidget      *widget, /* IN */
                         GtkRequisition *req)    /* OUT */
{
	g_return_if_fail(req != NULL);

	req->width = 150;
	req->height = 50;
}

/**
 * uber_graph_add_label:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_add_label (UberGraph *graph, /* IN */
                      UberLabel *label) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(UBER_IS_LABEL(label));

	priv = graph->priv;
	gtk_box_pack_start(GTK_BOX(priv->labels), GTK_WIDGET(label),
	                   TRUE, TRUE, 0);
	gtk_widget_show(GTK_WIDGET(label));
}

/**
 * uber_graph_button_press:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static gboolean
uber_graph_button_press_event (GtkWidget      *widget, /* IN */
                               GdkEventButton *button) /* IN */
{
	UberGraphPrivate *priv;

	g_return_val_if_fail(UBER_IS_GRAPH(widget), FALSE);

	priv = UBER_GRAPH(widget)->priv;

	switch (button->button) {
	case 2: /* Middle Click */
		priv->paused = !priv->paused;
		if (priv->fps_handler) {
			g_source_remove(priv->fps_handler);
			priv->fps_handler = 0;
		} else {
			priv->fg_dirty = TRUE;
			priv->full_draw = TRUE;
			uber_graph_register_fps_handler(UBER_GRAPH(widget));
		}
		break;
	default:
		break;
	}
	return FALSE;
}

/**
 * uber_graph_finalize:
 * @object: A #UberGraph.
 *
 * Finalizer for a #UberGraph instance.  Frees any resources held by
 * the instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_finalize (GObject *object) /* IN */
{
	G_OBJECT_CLASS(uber_graph_parent_class)->finalize(object);
}

/**
 * uber_graph_dispose:
 * @object: A #GObject.
 *
 * Dispose callback for @object.  This method releases references held
 * by the #GObject instance.
 *
 * Returns: None.
 * Side effects: Plenty.
 */
static void
uber_graph_dispose (GObject *object) /* IN */
{
	UberGraph *graph;
	UberGraphPrivate *priv;

	graph = UBER_GRAPH(object);
	priv = graph->priv;
	/*
	 * Stop any timeout handlers.
	 */
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
	}
	if (priv->dps_handler) {
		g_source_remove(priv->dps_handler);
	}
	/*
	 * Destroy textures.
	 */
	uber_graph_destroy_texture(graph);
	/*
	 * Destroy background resources.
	 */
	if (priv->bg_cairo) {
		cairo_destroy(priv->bg_cairo);
		priv->bg_cairo = NULL;
	}
	if (priv->bg_pixmap) {
		g_object_unref(priv->bg_pixmap);
		priv->bg_pixmap = NULL;
	}
	G_OBJECT_CLASS(uber_graph_parent_class)->dispose(object);
}

/**
 * uber_graph_class_init:
 * @klass: A #UberGraphClass.
 *
 * Initializes the #UberGraphClass and prepares the vtable.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_class_init (UberGraphClass *klass) /* IN */
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = uber_graph_finalize;
	object_class->dispose = uber_graph_dispose;
	g_type_class_add_private(object_class, sizeof(UberGraphPrivate));

	widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->expose_event = uber_graph_expose_event;
	widget_class->hide = uber_graph_hide;
	widget_class->realize = uber_graph_realize;
	widget_class->screen_changed = uber_graph_screen_changed;
	widget_class->show = uber_graph_show;
	widget_class->size_allocate = uber_graph_size_allocate;
	widget_class->style_set = uber_graph_style_set;
	widget_class->unrealize = uber_graph_unrealize;
	widget_class->size_request = uber_graph_size_request;
	widget_class->button_press_event = uber_graph_button_press_event;

	show_fps = !!g_getenv("UBER_SHOW_FPS");
}

/**
 * uber_graph_init:
 * @graph: A #UberGraph.
 *
 * Initializes the newly created #UberGraph instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_init (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	/*
	 * Store pointer to private data allocation.
	 */
	graph->priv = G_TYPE_INSTANCE_GET_PRIVATE(graph,
	                                          UBER_TYPE_GRAPH,
	                                          UberGraphPrivate);
	priv = graph->priv;
	/*
	 * Enable required events.
	 */
	gtk_widget_set_events(GTK_WIDGET(graph), GDK_BUTTON_PRESS_MASK);
	/*
	 * Prepare default values.
	 */
	priv->tick_len = 10;
	priv->fps = 20;
	priv->fps_real = 1000. / priv->fps;
	priv->dps = 1.;
	priv->x_slots = 60;
	priv->fg_dirty = TRUE;
	priv->bg_dirty = TRUE;
	priv->full_draw = TRUE;
	priv->show_xlines = TRUE;
	priv->show_ylines = TRUE;
	/*
	 * TODO: Support labels in a grid.
	 */
	priv->labels = gtk_hbox_new(TRUE, 3);
	priv->align = gtk_alignment_new(.5, .5, 1., 1.);
	gtk_container_add(GTK_CONTAINER(priv->align), priv->labels);
	gtk_widget_show(priv->labels);
}
