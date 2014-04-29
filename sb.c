/*
 * sb - simple browser
 * 
 * David Luco - <dluco11 at gmail dot com>
 * 
 * See LICENSE file for copyright and license details.
 */

#include <stdio.h>
#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include "config.h"

static GtkWidget* main_window;
static GtkWidget* menu_bar;

static GtkWidget* main_toolbar;
static GtkToolItem* back_button;
static GtkToolItem* forward_button;
static GtkToolItem* refresh_button;
static GtkWidget* uri_entry;

static GtkStatusbar* main_statusbar;
static WebKitWebView* web_view;
static gchar* main_title;
static gint load_progress;
static guint status_context_id;

/*
 * Callback for activation of the url-bar - open web page in web-view
 */
static void
activate_uri_entry_cb (GtkWidget* entry, gpointer data)
{
	const gchar* uri = gtk_entry_get_text (GTK_ENTRY (entry));
	g_assert (uri);
	webkit_web_view_open (web_view, uri);
}

/*
 * Update the title of the window with name of web page and load progress
 */
static void
update_title (GtkWindow* window)
{
	GString* string = g_string_new (main_title);
	g_string_append (string, " - sb");
	if (load_progress < 100)
		g_string_append_printf (string, " (%d%%)", load_progress);
	gchar* title = g_string_free (string, FALSE);
	gtk_window_set_title (window, title);
	g_free (title);
}

/*
 * Callback for hovering over a link - show in statusbar
 */
static void
link_hover_cb (WebKitWebView* page, const gchar* title, const gchar* link, gpointer data)
{
	/* underflow is allowed */
	gtk_statusbar_pop (main_statusbar, status_context_id);
	if (link)
		gtk_statusbar_push (main_statusbar, status_context_id, link);
}

/*
 * Callback for a change in the title of a web page - update window title
 */
static void
title_change_cb (WebKitWebView* web_view, WebKitWebFrame* web_frame, const gchar* title, gpointer data)
{
	if (main_title)
		g_free (main_title);
	main_title = g_strdup (title);
	update_title (GTK_WINDOW (main_window));
}

/*
 * Callback for change in progress of a page being loaded - update title of window
 */
static void
progress_change_cb (WebKitWebView* web_view, gint progress, gpointer data)
{
	load_progress = progress;
	update_title (GTK_WINDOW (main_window));
}

/*
 * Callback for a change in the load status of a web view
 */
static void
load_status_change_cb (WebKitWebView* web_view, gpointer data)
{
	WebKitWebFrame* frame;
	/*WebKitWebDataSource* source;
	WebKitNetworkRequest* request;*/
	const gchar* uri;
	
	switch (webkit_web_view_get_load_status (web_view))
	{
	case WEBKIT_LOAD_COMMITTED:
		/* Update uri in entry-bar */
		frame = webkit_web_view_get_main_frame (web_view);
		uri = webkit_web_frame_get_uri (frame);
		if (uri)
			gtk_entry_set_text (GTK_ENTRY (uri_entry), uri);
		break;
	case WEBKIT_LOAD_FINISHED:
		/* Update buttons - back, forward */
		gtk_widget_set_sensitive (GTK_WIDGET (back_button), webkit_web_view_can_go_back (web_view));
		gtk_widget_set_sensitive (GTK_WIDGET (forward_button), webkit_web_view_can_go_forward (web_view));
		break;
	default:
		break;
	}
}


/*
 * Callback to exit program
 */
static void
destroy_cb (GtkWidget* widget, gpointer data)
{
	gtk_main_quit ();
}

/*
 * Callback for open-file menu item - open file-chooser dialog and open selected file in webview
 */
static void
openfile_cb (GtkWidget* widget, gpointer data)
{
	GtkWidget* file_dialog = gtk_file_chooser_dialog_new ("Open File",
														GTK_WINDOW (main_window),
														GTK_FILE_CHOOSER_ACTION_OPEN,
														"Cancel", GTK_RESPONSE_CANCEL,
														"Open", GTK_RESPONSE_ACCEPT,
														NULL);
	
	/* Add filters to dialog - all and html files */
	GtkFileFilter* filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, "All files");
	gtk_file_filter_add_pattern (filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog), filter);
	
	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, "HTML files");
	gtk_file_filter_add_mime_type (filter, "text/html");
	gtk_file_filter_add_pattern (filter, "*.htm");
	gtk_file_filter_add_pattern (filter, "*.html");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog), filter);
	
	/* Run the dialog and check result. If a file was selected, open it in the web-view. */							
	if (gtk_dialog_run (GTK_DIALOG (file_dialog)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_dialog));
		webkit_web_view_open (web_view, filename);
		g_free (filename);
	}
	
	gtk_widget_destroy (file_dialog);
}

static void
print_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_frame_print (webkit_web_view_get_main_frame (web_view));
}

/*
 * Callback for edit.cut
 */
static void
cut_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_cut_clipboard (web_view);
}

/*
 * Callback for edit.copy
 */
static void
copy_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_copy_clipboard (web_view);
}

/*
 * Callback for edit.paste
 */
static void
paste_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_paste_clipboard (web_view);
}

/* 
 * Callback for edit.delete
 */
static void
delete_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_delete_selection (web_view);
}

/*
 * Callback for options menu - toggle indicated setting
 */
static void
options_cb (GtkWidget* widget, gpointer data)
{
	WebKitWebSettings *settings = webkit_web_view_get_settings (web_view);
	
	g_object_set (G_OBJECT (settings), "enable-smooth-scrolling", TRUE, NULL);
	
	webkit_web_view_set_settings (WEBKIT_WEB_VIEW(web_view), settings);
	
	return;
}

/*
 * Callback for about menu item - open about dialog
 */
static void
about_cb (GtkWidget* widget, gpointer data)
{
	GtkWidget* about_dialog = gtk_about_dialog_new ();
	
	const gchar *authors[] = {"David Luco", "<dluco11@gmail.com>", NULL};
	
	/* Set program name and comments */
	gtk_about_dialog_set_program_name (GTK_ABOUT_DIALOG (about_dialog), "sb");
	gtk_about_dialog_set_comments (GTK_ABOUT_DIALOG (about_dialog), "A simple webkit/gtk browser, in the style of surf by suckless.org");
	
	/* Set logo to display in dialog */
	gtk_about_dialog_set_logo_icon_name (GTK_ABOUT_DIALOG (about_dialog), "browser");
	/* Set taskbar/window icon */
	gtk_window_set_icon_name (GTK_WINDOW (about_dialog), "browser");
	
	/* Set authors, license, and website in dialog */
	gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (about_dialog), authors);
	gtk_about_dialog_set_license (GTK_ABOUT_DIALOG (about_dialog), "Distributed under the MIT license.\nhttp://www.opensource.org/licenses/mit-license.php");
	gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (about_dialog), "http://dluco.github.io/sb/");
	
	gtk_dialog_run (GTK_DIALOG (about_dialog));
	
	gtk_widget_destroy (about_dialog);
}

/*
 * Callback for back button
 */
static void
go_back_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_go_back (web_view);
}

/*
 * Callback for forward button
 */
static void
go_forward_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_go_forward (web_view);
}

/*
 * Callback for refresh button - reload web page
 */
static void
refresh_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_reload (web_view);
}

/*
 * Callback for home button - open home page
 */
static void
home_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_open (web_view, home_page);
}

/*
 * Create a webkit webview instance in a scrolled window
 */
static GtkWidget*
create_browser ()
{
	GtkWidget* scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	web_view = WEBKIT_WEB_VIEW (webkit_web_view_new ());
	
	
	gtk_container_add (GTK_CONTAINER (scrolled_window), GTK_WIDGET (web_view));

	g_signal_connect (G_OBJECT (web_view), "title-changed", G_CALLBACK (title_change_cb), web_view);
	g_signal_connect (G_OBJECT (web_view), "load-progress-changed", G_CALLBACK (progress_change_cb), web_view);
	g_signal_connect (G_OBJECT (web_view), "notify::load-status", G_CALLBACK (load_status_change_cb), web_view);
	g_signal_connect (G_OBJECT (web_view), "hovering-over-link", G_CALLBACK (link_hover_cb), web_view);

	return scrolled_window;
}

/*
 * Set up menubar - file, edit, options, and help menus
 */
static GtkWidget*
create_menubar ()
{
	/* Create menubar */
	menu_bar = gtk_menu_bar_new ();
	gtk_widget_show (menu_bar);
	
	/* Create File, Edit, and Help Menus */
	GtkWidget* file_menu = gtk_menu_new ();
	GtkWidget* edit_menu = gtk_menu_new ();
	GtkWidget* options_menu = gtk_menu_new ();
	GtkWidget* help_menu = gtk_menu_new ();
	
	/* Create the menu items (and set icons) */
	GtkWidget* open_item = gtk_menu_item_new_with_label ("Open");
	GtkWidget* print_item = gtk_menu_item_new_with_label ("Print");
	GtkWidget* quit_item = gtk_menu_item_new_with_label ("Quit");
	GtkWidget* cut_item = gtk_menu_item_new_with_label ("Cut");
	GtkWidget* copy_item = gtk_menu_item_new_with_label ("Copy");
	GtkWidget* paste_item = gtk_menu_item_new_with_label ("Paste");
	GtkWidget* delete_item = gtk_menu_item_new_with_label ("Delete");
	GtkWidget* smooth_scrolling_item = gtk_menu_item_new_with_label ("Smooth Scrolling");
	GtkWidget* about_item = gtk_menu_item_new_with_label ("About");
	
	/* Add them to the appropriate menu */
	gtk_menu_append (GTK_MENU (file_menu), open_item);
	gtk_menu_append (GTK_MENU (file_menu), print_item);
	gtk_menu_append (GTK_MENU (file_menu), quit_item);
	gtk_menu_append (GTK_MENU (edit_menu), cut_item);
	gtk_menu_append (GTK_MENU (edit_menu), copy_item);
	gtk_menu_append (GTK_MENU (edit_menu), paste_item);
	gtk_menu_append (GTK_MENU (edit_menu), delete_item);
	gtk_menu_append (GTK_MENU (options_menu), smooth_scrolling_item);
	gtk_menu_append (GTK_MENU (help_menu), about_item);
	
	/* Attach the callback functions to the activate signal */
	gtk_signal_connect_object (GTK_OBJECT (open_item), "activate", GTK_SIGNAL_FUNC (openfile_cb), (gpointer) "file.open");
	gtk_signal_connect_object (GTK_OBJECT (print_item), "activate", GTK_SIGNAL_FUNC (print_cb), (gpointer) "file.print");
	gtk_signal_connect_object (GTK_OBJECT (quit_item), "activate", GTK_SIGNAL_FUNC (destroy_cb), (gpointer) "file.quit");
	gtk_signal_connect_object (GTK_OBJECT (cut_item), "activate", GTK_SIGNAL_FUNC (cut_cb), (gpointer) "edit.cut");
	gtk_signal_connect_object (GTK_OBJECT (copy_item), "activate", GTK_SIGNAL_FUNC (copy_cb), (gpointer) "edit.copy");
	gtk_signal_connect_object (GTK_OBJECT (paste_item), "activate", GTK_SIGNAL_FUNC (paste_cb), (gpointer) "edit.paste");
	gtk_signal_connect_object (GTK_OBJECT (delete_item), "activate", GTK_SIGNAL_FUNC (delete_cb), (gpointer) "edit.delete");
	gtk_signal_connect_object (GTK_OBJECT (smooth_scrolling_item), "activate", GTK_SIGNAL_FUNC (options_cb), (gpointer) "options.smooth-scrolling");
	gtk_signal_connect_object (GTK_OBJECT (about_item), "activate", GTK_SIGNAL_FUNC (about_cb), (gpointer) "help.about");
	
	/* Show menu items */
	gtk_widget_show (open_item);
	gtk_widget_show (print_item);
	gtk_widget_show (quit_item);
	gtk_widget_show (cut_item);
	gtk_widget_show (copy_item);
	gtk_widget_show (paste_item);
	gtk_widget_show (delete_item);
	gtk_widget_show (smooth_scrolling_item);
	gtk_widget_show (about_item);
	
	/* Create "File" and "Help" entries in menubar */
	GtkWidget* file_item = gtk_menu_item_new_with_label ("File");
	GtkWidget* edit_item = gtk_menu_item_new_with_label ("Edit");
	GtkWidget* options_item = gtk_menu_item_new_with_label ("Options");
	GtkWidget* help_item = gtk_menu_item_new_with_label ("Help");
	gtk_widget_show (file_item);
	gtk_widget_show (edit_item);
	gtk_widget_show (options_item);
	gtk_widget_show (help_item);
	
	/* Associate file_menu with file_item in the menubar */
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (file_item), file_menu);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (edit_item), edit_menu);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (options_item), options_menu);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (help_item), help_menu);
	
	/* Add file_menu to menu_bar */
	gtk_menu_bar_append (GTK_MENU_BAR (menu_bar), file_item);
	gtk_menu_bar_append (GTK_MENU_BAR (menu_bar), edit_item);
	gtk_menu_bar_append (GTK_MENU_BAR (menu_bar), options_item);
	gtk_menu_bar_append (GTK_MENU_BAR (menu_bar), help_item);
	
	return (GtkWidget*)menu_bar;
}

/*
 * Create statusbar - when hovering over a link, show in statusbar
 */
static GtkWidget*
create_statusbar ()
{
	main_statusbar = GTK_STATUSBAR (gtk_statusbar_new ());
	status_context_id = gtk_statusbar_get_context_id (main_statusbar, "Link Hover");

	return (GtkWidget*)main_statusbar;
}

/*
 * Create the toolbar: back, forward, refresh, urlbar, home button
 */
static GtkWidget*
create_toolbar ()
{
	GtkWidget* toolbar = gtk_toolbar_new ();

	gtk_toolbar_set_orientation (GTK_TOOLBAR (toolbar), GTK_ORIENTATION_HORIZONTAL);
	gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_BOTH_HORIZ);

	/* the back button */
	back_button = gtk_tool_button_new_from_stock (GTK_STOCK_GO_BACK);
	g_signal_connect (G_OBJECT (back_button), "clicked", G_CALLBACK (go_back_cb), NULL);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), GTK_TOOL_ITEM (back_button), -1);

	/* The forward button */
	forward_button = gtk_tool_button_new_from_stock (GTK_STOCK_GO_FORWARD);
	g_signal_connect (G_OBJECT (forward_button), "clicked", G_CALLBACK (go_forward_cb), NULL);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), GTK_TOOL_ITEM (forward_button), -1);
	
	/* The refresh button */
	refresh_button = gtk_tool_button_new_from_stock (GTK_STOCK_REFRESH);
	g_signal_connect (G_OBJECT (refresh_button), "clicked", G_CALLBACK (refresh_cb), NULL);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), GTK_TOOL_ITEM (refresh_button), -1);
	
	GtkToolItem* item;

	/* The URL entry */
	item = gtk_tool_item_new ();
	gtk_tool_item_set_expand (item, TRUE);
	uri_entry = gtk_entry_new ();
	/*gtk_entry_set_icon_from_stock (GTK_ENTRY (uri_entry), GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_MEDIA_PLAY);*/
	gtk_container_add (GTK_CONTAINER (item), uri_entry);
	g_signal_connect (G_OBJECT (uri_entry), "activate", G_CALLBACK (activate_uri_entry_cb), NULL);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

	/* The home button */
	item = gtk_tool_button_new_from_stock (GTK_STOCK_HOME);
	g_signal_connect (G_OBJECT (item), "clicked", G_CALLBACK (home_cb), NULL);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

	return toolbar;
}

/*
 * Create the main window, set name and icon.
 * Default geometry = 800x600
 */
static GtkWidget*
create_window ()
{
	GtkWidget* window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
	gtk_widget_set_name (window, "sb");
	gtk_window_set_icon_name (GTK_WINDOW (window), "browser");
	g_signal_connect (G_OBJECT (window), "destroy", G_CALLBACK (destroy_cb), NULL);

	return window;
}

/*
 * Apply default settings to web-view
 */
static void
set_settings ()
{
	WebKitWebSettings *settings = webkit_web_settings_new ();
	
	/* Apply default settings from config.h */
	g_object_set (G_OBJECT (settings), "user-agent", useragent, NULL);
	g_object_set (G_OBJECT (settings), "auto-load-images", loadimages, NULL);
	g_object_set (G_OBJECT (settings), "enable-plugins", enableplugins, NULL);
	g_object_set (G_OBJECT (settings), "enable-scripts", enablescripts, NULL);
	g_object_set (G_OBJECT (settings), "enable-spatial-navigation", enablespatialbrowsing, NULL);
	g_object_set (G_OBJECT (settings), "enable-spell-checking", enablespellchecking, NULL);
	g_object_set (G_OBJECT (settings), "enable-developer-extras", enableinspector, NULL);
	
	/* Apply settings */
	webkit_web_view_set_settings (WEBKIT_WEB_VIEW(web_view), settings);
}

/*
 * Main function of program
 */
int
main (int argc, char* argv[])
{
	gtk_init (&argc, &argv);

	GtkWidget* vbox = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), create_menubar (), FALSE, FALSE, 0);
	main_toolbar = create_toolbar ();
	gtk_box_pack_start (GTK_BOX (vbox), main_toolbar, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), create_browser (), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), create_statusbar (), FALSE, FALSE, 0);

	main_window = create_window ();
	gtk_container_add (GTK_CONTAINER (main_window), vbox);
	
	set_settings ();
	gchar* uri = (gchar*) (argc > 1 ? argv[1] : home_page);
	webkit_web_view_open (web_view, uri);

	gtk_widget_grab_focus (GTK_WIDGET (web_view));
	gtk_widget_show_all (main_window);
	gtk_main ();

	return 0;
}
