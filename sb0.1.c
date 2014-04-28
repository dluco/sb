#include <gtk/gtk.h>
#include <webkit/webkit.h>

static void destroyWindowCb(GtkWidget *widget, GtkWidget *window)
{
	gtk_main_quit();
}

static gboolean closeWebViewCb(WebKitWebView *web_view, GtkWidget *window)
{
	gtk_widget_destroy(window);
	return TRUE;
}

int main(int argc, char *argv[])
{
	char *default_site = "http://www.google.com/";
	
	/* Initialize GTK+ */
	gtk_init(&argc, &argv);
	
	/* Create an 800x600 window that will hold the browser instance */
	GtkWidget *main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(main_window), 800,600);
	
	/* Create a browser instance */
	WebKitWebView *web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
	
	/* Create a scrollable area, and put the browser instance into it */
	GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrolled_window), GTK_WIDGET(web_view));
	
	/* Set up callbacks so that if either the main window or
	 * the browser instance is closed, the program will exit */
	g_signal_connect(main_window, "destroy", G_CALLBACK(destroyWindowCb), NULL);
	g_signal_connect(web_view, "close-web-view", G_CALLBACK(closeWebViewCb), main_window);
	
	/* Put the scrollable area into the main window */
	gtk_container_add(GTK_CONTAINER(main_window), scrolled_window);
	
	/* Load default web page into the browser instance */
	webkit_web_view_load_uri(web_view, default_site);
	
	/* Make sure that when the browser area becomes visible,
	 * it will get mouse and keyboard events */
	gtk_widget_grab_focus(GTK_WIDGET(web_view));
	
	/* Make sure the main window and all its contents are visible */
	gtk_widget_show_all(main_window);
	
	/* Run the main GTK+ event loop */
	gtk_main();
	
	return 0;
}
