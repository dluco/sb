#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include "config.h"

static GtkWidget* main_window;
static GtkWidget* uri_entry;
static GtkStatusbar* main_statusbar;
static WebKitWebView* web_view;
static gchar* main_title;
static gint load_progress;
static guint status_context_id;

static void destroy_cb(GtkWidget *widget, GtkWidget *window)
{
	gtk_main_quit();
}

static gboolean close_web_view_cb(WebKitWebView *web_view, GtkWidget *window)
{
	gtk_widget_destroy(window);
	return TRUE;
}

static void link_hover_cb(WebKitWebView* page, const gchar* title, const gchar* link, gpointer data)
{
    /* underflow is allowed */
    gtk_statusbar_pop(main_statusbar, status_context_id);
    if(link)
        gtk_statusbar_push(main_statusbar, status_context_id, link);
}

static void title_change_cb(WebKitWebView* web_view, WebKitWebFrame* web_frame, const gchar* title, gpointer data)
{
    if(main_title)
        g_free(main_title);
    main_title = g_strdup(title);
    update_title(GTK_WINDOW(main_window));
}

static void progress_change_cb(WebKitWebView* page, gint progress, gpointer data)
{
    load_progress = progress;
    update_title(GTK_WINDOW(main_window));
}

static void load_commit_cb(WebKitWebView* page, WebKitWebFrame* frame, gpointer data)
{
    const gchar* uri = webkit_web_frame_get_uri(frame);
    if(uri)
        gtk_entry_set_text(GTK_ENTRY(uri_entry), uri);
}

/* Create an 800x600 window that will hold the browser instance */
static GtkWidget* create_window()
{
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW (window), 800, 600);
    gtk_widget_set_name(window, "sb");
    
    /* Set up destroy callback for the window */
    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(destroy_cb), NULL);

    return window;
}

/* Create a webkit web-view browser instance in a scrolled window */
static GtkWidget* create_browser()
{
    GtkWidget* scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	
	/* Create web-view and add to scrolled window */
    web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    gtk_container_add(GTK_CONTAINER (scrolled_window), GTK_WIDGET(web_view));
	
	/* Set up callbacks for the web-view */
    g_signal_connect(G_OBJECT(web_view), "title-changed", G_CALLBACK(title_change_cb), web_view);
    g_signal_connect(G_OBJECT(web_view), "load-progress-changed", G_CALLBACK(progress_change_cb), web_view);
    g_signal_connect(G_OBJECT(web_view), "load-committed", G_CALLBACK(load_commit_cb), web_view);
    g_signal_connect(G_OBJECT(web_view), "hovering-over-link", G_CALLBACK(link_hover_cb), web_view);
    g_signal_connect(G_OBJECT(web_view), "close-web-view", G_CALLBACK(close_web_view_cb), main_window);

    return scrolled_window;
}

int main(int argc, char *argv[])
{	
	/* Initialize GTK+ */
	gtk_init(&argc, &argv);
	
	/* Put the scrollable area into the main window */
	gtk_container_add(GTK_CONTAINER(main_window), scrolled_window);
	
	/* Load default web page into the browser instance */
	webkit_web_view_load_uri(web_view, home_page);
	
	/* Make sure that when the browser area becomes visible,
	 * it will get mouse and keyboard events */
	gtk_widget_grab_focus(GTK_WIDGET(web_view));
	
	/* Make sure the main window and all its contents are visible */
	gtk_widget_show_all(main_window);
	
	/* Run the main GTK+ event loop */
	gtk_main();
	
	return 0;
}
