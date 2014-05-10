/*
 * sb - simple browser
 * 
 * David Luco - <dluco11 at gmail dot com>
 * 
 * See LICENSE file for copyright and license details.
 */

#include <stdio.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <webkit/webkit.h>

#include "config.h"

static GtkWidget* main_window;
static GtkWidget* menu_bar;

static GtkWidget* main_toolbar;
static GtkToolItem* back_button;
static GtkToolItem* forward_button;
static GtkToolItem* refresh_button;
static GtkWidget* uri_entry;
static GtkWidget* search_engine_entry;

static GtkStatusbar* main_statusbar;
static WebKitWebView* web_view;
static gchar* main_title;
static gint load_progress;
static guint status_context_id;

static GtkAccelGroup *accel_group = NULL;
static gboolean fullscreen = FALSE;

static GtkEntryBuffer* find_buffer;
static char* useragents[] = {
	"Mozilla/5.0 (X11; U; Unix; en-US) AppleWebKit/537.15 (KHTML, like Gecko) Chrome/24.0.1295.0 Safari/537.15 sb/0.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:31.0) Gecko/20100101 Firefox/31.0",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_2) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/36.0.1944.0 Safari/537.36",
	"Mozilla/5.0 (compatible; MSIE 10.6; Windows NT 6.1; Trident/5.0; InfoPath.2; SLCC1; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 2.0.50727) 3gpp-gba UNTRUSTED/1.0",
	"Mozilla/5.0 (Linux; U; Android 4.0.3; ko-kr; LG-L160L Build/IML74K) AppleWebkit/534.30 (KHTML, like Gecko) Version/4.0 Mobile Safari/534.30",
};

typedef struct engine {
	char* name;
	char* url;
} engine;

static int search_engine_current = 0;
static engine search_engines[] = {
	{"Google", "https://www.google.ca/#q="},
	{"Duck Duck Go", "https://duckduckgo.com/?q="}
};

/*
 * Callback for activation of the url-bar - open web page in web-view
 */
static void
activate_uri_entry_cb (GtkWidget* entry, gpointer data)
{
	const gchar* uri;
	const gchar* temp = gtk_entry_get_text (GTK_ENTRY (entry));
	
	/* Append appropriate prefix */
	if (temp[0] == '/')
		uri = g_strdup_printf("file://%s", temp);
	else
		uri = g_strrstr(temp, "://") ? g_strdup(temp) : g_strdup_printf("http://%s", temp);
	
	webkit_web_view_load_uri (web_view, uri);
}

static void
activate_search_engine_entry_cb (GtkWidget* entry, gpointer data)
{
	const gchar* uri = g_strconcat (search_engines[search_engine_current].url, gtk_entry_get_text (GTK_ENTRY (entry)), NULL);
	g_assert (uri);
	webkit_web_view_load_uri (web_view, uri);
}

static void
choose_search_engine_dialog ()
{
	GtkWidget *dialog;
	GtkWidget *vbox;
	GtkWidget *combo_box;
	
	dialog = gtk_dialog_new_with_buttons ("Engine...",
													GTK_WINDOW (main_window),
													GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
													GTK_STOCK_CANCEL,
													GTK_RESPONSE_REJECT,
													GTK_STOCK_OK,
													GTK_RESPONSE_ACCEPT,
													NULL);
													
	vbox = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	
	combo_box = gtk_combo_box_text_new ();
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo_box), "Google");
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo_box), "Duck Duck Go");
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), search_engine_current);
	gtk_box_pack_start (GTK_BOX (vbox), combo_box, FALSE, FALSE, 10);
	gtk_widget_show (combo_box);
	
	gint result = gtk_dialog_run (GTK_DIALOG (dialog));
	switch (result)
	{
		case GTK_RESPONSE_ACCEPT:
			search_engine_current = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box));
			break;
		default:
			break;
	}
	
	gtk_widget_destroy (dialog);
}

static void
search_engine_entry_icon_cb (GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEvent *event, gpointer data)
{
	switch (icon_pos)
	{
		case GTK_ENTRY_ICON_PRIMARY:
			choose_search_engine_dialog ();
			break;
		case GTK_ENTRY_ICON_SECONDARY:
			activate_search_engine_entry_cb (GTK_WIDGET (entry), data);
			break;
	}
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
 * Decide if the web page/file can be viewed - if not, then download it
 */
static gboolean
decide_download_cb (WebKitWebView* web_view, WebKitWebFrame* frame, WebKitNetworkRequest* request, gchar* mimetype,  WebKitWebPolicyDecision* policy_decision, gpointer data)
{
	if (!webkit_web_view_can_show_mime_type (web_view, mimetype))
	{
		webkit_web_policy_decision_download (policy_decision);
		return TRUE;
	}
	return FALSE;
}

/*
 * Start the download of a file - use the server-recommended file name
 */
static gboolean
init_download_cb (WebKitWebView* web_view, WebKitDownload* download, gpointer data)
{
	const gchar* uri = g_strconcat ("file://", download_dir, webkit_download_get_suggested_filename (download), NULL);
	webkit_download_set_destination_uri (download, uri);
	return TRUE;
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
 * Callback for file.open - open file-chooser dialog and open selected file in webview
 */
static void
openfile_cb (GtkWidget* widget, gpointer data)
{
	GtkWidget* file_dialog;
	GtkFileFilter* filter;
	gchar *filename;
	
	file_dialog = gtk_file_chooser_dialog_new ("Open File",
														GTK_WINDOW (main_window),
														GTK_FILE_CHOOSER_ACTION_OPEN,
														"Cancel", GTK_RESPONSE_CANCEL,
														"Open", GTK_RESPONSE_ACCEPT,
														NULL);
	
	/* Add filters to dialog - all and html files */
	filter = gtk_file_filter_new ();
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
		filename = g_strdup_printf("file://%s", gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_dialog)));
		webkit_web_view_load_uri (web_view, filename);
		g_free (filename);
	}
	
	gtk_widget_destroy (file_dialog);
}

/*
 * Open print dialog for the current web page
 */
static void
print_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_frame_print (webkit_web_view_get_main_frame (web_view));
}

/*
 * Callback for edit.cut - cut current selection
 */
static void
cut_cb (GtkWidget* widget, gpointer data)
{	
	if (gtk_widget_has_focus (uri_entry))
	{
		g_signal_emit_by_name (uri_entry, "cut-clipboard");
	} else if (gtk_widget_has_focus (search_engine_entry))
	{
		g_signal_emit_by_name (search_engine_entry, "cut-clipboard");
	} else if (TRUE)
	{
		webkit_web_view_cut_clipboard (web_view);
	}
}

/*
 * Callback for edit.copy - copy current selection
 */
static void
copy_cb (GtkWidget* widget, gpointer data)
{	
	if (gtk_widget_has_focus (uri_entry))
	{
		g_signal_emit_by_name (uri_entry, "copy-clipboard");
	} else if (gtk_widget_has_focus (search_engine_entry))
	{
		g_signal_emit_by_name (search_engine_entry, "copy-clipboard");
	} else if (TRUE)
	{
		webkit_web_view_copy_clipboard (web_view);
	}
}

/*
 * Callback for edit.paste - paste current selection
 */
static void
paste_cb (GtkWidget* widget, gpointer data)
{
	if (gtk_widget_has_focus (uri_entry))
	{
		g_signal_emit_by_name (uri_entry, "paste-clipboard");
	} else if (gtk_widget_has_focus (search_engine_entry))
	{
		g_signal_emit_by_name (search_engine_entry, "paste-clipboard");
	} else if (TRUE)
	{
		webkit_web_view_paste_clipboard (web_view);
	}
}

/* 
 * Callback for edit.delete - delete current selection
 */
static void
delete_cb (GtkWidget* widget, gpointer data)
{	
	if (gtk_widget_has_focus (uri_entry))
	{
		g_signal_emit_by_name (uri_entry, "delete-from-cursor");
	} else if (gtk_widget_has_focus (search_engine_entry))
	{
		g_signal_emit_by_name (search_engine_entry, "delete-from-cursor");
	} else if (TRUE)
	{
		webkit_web_view_delete_selection (web_view);
	}
}

/*
 * Dialog to search for text in current web-view
 */
static void
find_dialog_cb (GtkWidget* widget, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *content_area;
	GtkWidget *find_button;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *find_entry;
	GtkWidget *case_button;
	GtkWidget *find_forward_button;
	GtkWidget *wrap_button;
	
	dialog = gtk_dialog_new_with_buttons ("Find",
													GTK_WINDOW (main_window),
													GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
													GTK_STOCK_CANCEL,
													GTK_RESPONSE_REJECT,
													GTK_STOCK_FIND,
													GTK_RESPONSE_ACCEPT,
													NULL);
	
	/* Get area for entry and toggles, and get the find button */
	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	find_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
	
	/* Set "Find" button as default button */
	gtk_widget_set_can_default (find_button, TRUE);
	gtk_widget_grab_default (find_button);
	
	/* Create a hbox to hold label and entry */
	hbox = gtk_hbox_new (FALSE, 0);
	label = gtk_label_new ("Find what:");
	
	/* Set up entry - entry activation activates the find button as well */
	find_entry = gtk_entry_new_with_buffer (GTK_ENTRY_BUFFER (find_buffer));
	gtk_entry_set_activates_default (GTK_ENTRY (find_entry), TRUE);
	
	/* Pack label and entry into hbox */
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 5);
	gtk_box_pack_start (GTK_BOX (hbox), find_entry, FALSE, FALSE, 5);
	
	/* Pack hbox into the content area */
	gtk_box_pack_start (GTK_BOX (content_area), hbox, FALSE, FALSE, 5);
	
	/* Set up toggles for matching case, searching forward, and wrapping the search */
	case_button = gtk_check_button_new_with_label ("Match case");
	gtk_box_pack_start (GTK_BOX (content_area), case_button, FALSE, FALSE, 5);
	
	find_forward_button = gtk_check_button_new_with_label ("Search forward");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (find_forward_button), TRUE);
	gtk_box_pack_start (GTK_BOX (content_area), find_forward_button, FALSE, FALSE, 5);
	
	wrap_button = gtk_check_button_new_with_label ("Wrap search");
	gtk_box_pack_start (GTK_BOX (content_area), wrap_button, FALSE, FALSE, 5);
	
	/* Show all widgets */
	gtk_widget_show (label);
	gtk_widget_show (find_entry);
	gtk_widget_show (hbox);
	gtk_widget_show (case_button);
	gtk_widget_show (wrap_button);
	gtk_widget_show (find_forward_button);
	
	/* Run dialog and check result */
	gint result = gtk_dialog_run (GTK_DIALOG (dialog));
	switch (result)
	{
		/* "Find" button was pressed/activated - perform search */
		case GTK_RESPONSE_ACCEPT:
			webkit_web_view_search_text (web_view,
										gtk_entry_get_text (GTK_ENTRY (find_entry)),
										gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (case_button)),
										gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (find_forward_button)),
										gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (wrap_button)));
			break;
		/* Cancelled or closed - do nothing */
		default:
			break;
	}

	/* Destroy the dialog */
	gtk_widget_destroy (dialog);
}

static void
find_next_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_search_text (web_view, gtk_entry_buffer_get_text (find_buffer), FALSE, TRUE, TRUE);
}

/*
 * Zoom in web-view by 10%
 */
static void
zoom_in_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_zoom_in (web_view);
}

/*
 * Zoom out web-view by 10%
 */
static void
zoom_out_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_zoom_out (web_view);
}

/*
 * Reset zoom level to 100%
 */
static void
zoom_reset_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_set_zoom_level (web_view, 1.0);
}

static void
fullscreen_cb (GtkWidget* widget, gpointer data)
{
	if (!fullscreen)
	{
		gtk_window_fullscreen (GTK_WINDOW (main_window));
		fullscreen = TRUE;
	} else
	{
		gtk_window_unfullscreen (GTK_WINDOW (main_window));
		fullscreen = FALSE;
	}
}

/*
 * Settings dialog - set smooth-scrolling, private browsing, useragent
 */
static void
settings_dialog_cb (GtkWidget* widget, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *vbox;
	WebKitWebSettings *settings;
	GtkWidget *smooth_scrolling_button;
	GtkWidget *private_browsing_button;
	GtkWidget *inspector_button;
	GtkWidget *combo_box;
	
	dialog = gtk_dialog_new_with_buttons ("sb Settings",
													GTK_WINDOW (main_window),
													GTK_DIALOG_DESTROY_WITH_PARENT,
													GTK_STOCK_CANCEL,
													GTK_RESPONSE_REJECT,
													GTK_STOCK_OK,
													GTK_RESPONSE_ACCEPT,
													NULL);
	
	vbox = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	settings = webkit_web_view_get_settings (web_view);
	gboolean isactive = FALSE;
	
	/* Check-button to control smooth-scrolling */
	smooth_scrolling_button = gtk_check_button_new_with_label ("Enable smooth-scrolling");
	g_object_get (G_OBJECT (settings), "enable-smooth-scrolling", &isactive, NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (smooth_scrolling_button), isactive);
	gtk_box_pack_start (GTK_BOX (vbox), smooth_scrolling_button, TRUE, TRUE, 2);
	gtk_widget_show (smooth_scrolling_button);
	
	/* Check-button to control private browsing */
	private_browsing_button = gtk_check_button_new_with_label ("Enable private browsing");
	g_object_get (G_OBJECT (settings), "enable-private-browsing", &isactive, NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (private_browsing_button), isactive);
	gtk_box_pack_start (GTK_BOX (vbox), private_browsing_button, TRUE, TRUE, 2);
	gtk_widget_show (private_browsing_button);
	
	inspector_button = gtk_check_button_new_with_label ("Enable web inspector");
	g_object_get (G_OBJECT (settings), "enable-developer-extras", &isactive, NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (inspector_button), isactive);
	gtk_box_pack_start (GTK_BOX (vbox), inspector_button, TRUE, TRUE, 2);
	gtk_widget_show (inspector_button);
	
	/* Combox-box to choose the useragent to use */
	combo_box = gtk_combo_box_text_new ();
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo_box), "sb (Default)");
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo_box), "Firefox");
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo_box), "Chrome");
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo_box), "Internet Explorer");
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo_box), "Mobile");
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);
	gtk_box_pack_start (GTK_BOX (vbox), combo_box, FALSE, FALSE, 5);
	gtk_widget_show (combo_box);
	
	gint result = gtk_dialog_run (GTK_DIALOG (dialog));
	switch (result)
	{
		case GTK_RESPONSE_ACCEPT:
			/* Set smooth-scrolling from selection */
			g_object_set (G_OBJECT (settings), "enable-smooth-scrolling", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (smooth_scrolling_button)), NULL);
			
			/* Set private browsing from selection */
			g_object_set (G_OBJECT (settings), "enable-private-browsing", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (private_browsing_button)), NULL);
			
			g_object_set (G_OBJECT (settings), "enable-developer-extras", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (inspector_button)), NULL);
			
			/* Set user-agent from selection */
			g_object_set (G_OBJECT (settings), "user-agent", useragents[gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box))], NULL);
			webkit_web_view_set_settings (WEBKIT_WEB_VIEW(web_view), settings);
			
			break;
		default:
			break;
	}
	
	/* Destroy the dialog */
	gtk_widget_destroy (dialog);
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
	gtk_about_dialog_set_comments (GTK_ABOUT_DIALOG (about_dialog), "A simple webkit/gtk browser, in the style of surf and midori");
	
	/* Set logo to display in dialog */
	gtk_about_dialog_set_logo_icon_name (GTK_ABOUT_DIALOG (about_dialog), "web-browser");
	/* Set taskbar/window icon */
	gtk_window_set_icon_name (GTK_WINDOW (about_dialog), "web-browser");
	
	/* Set authors, license, and website in dialog */
	gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (about_dialog), authors);
	gtk_about_dialog_set_license (GTK_ABOUT_DIALOG (about_dialog), "Distributed under the MIT license.\nhttp://www.opensource.org/licenses/mit-license.php");
	gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (about_dialog), "http://dluco.github.io/sb/");
	
	gtk_dialog_run (GTK_DIALOG (about_dialog));
	
	gtk_widget_destroy (about_dialog);
}

static void
hide_menu_bar_cb (GtkWidget *widget, gpointer data)
{
	if (gtk_widget_get_visible (menu_bar) == TRUE)
	{
		gtk_widget_hide (menu_bar);
		return;
	}
	gtk_widget_show (menu_bar);
	return;
}

/*
 * Display context menu for menubar
 */
static void
view_context_menu (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	GtkWidget *menu;
	GtkWidget *hide_menu_bar_item;
	
	menu = gtk_menu_new ();

	hide_menu_bar_item = gtk_check_menu_item_new_with_label ("Menu Bar");
	
	/* Set initial state of toggle to show if menubar is visible */
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (hide_menu_bar_item), gtk_widget_get_visible (menu_bar));
	
	g_signal_connect (hide_menu_bar_item, "activate", G_CALLBACK (hide_menu_bar_cb), data);
	
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), hide_menu_bar_item);
	
	gtk_widget_show_all (menu);
	
	/* Trigger the popup menu to appear */
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, (event != NULL) ? event->button : 0, gdk_event_get_time ((GdkEvent*)event));
}

/*
 * Callback for pop-up context menu
 * 
 * Determine if right mouse button was clicked on the menubar or toolbar
 */
static gboolean
context_menu_cb (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	/* single click with the right mouse button? */
	if (event->type == GDK_BUTTON_PRESS && event->button == 3)
	{
		view_context_menu (widget, event, data);
		return TRUE;
	}
	return FALSE;
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
	webkit_web_view_load_uri (web_view, home_page);
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
	g_signal_connect (G_OBJECT (web_view), "mime-type-policy-decision-requested", G_CALLBACK (decide_download_cb), web_view);
	g_signal_connect (G_OBJECT (web_view), "download-requested", G_CALLBACK (init_download_cb), web_view);

	return scrolled_window;
}

/*
 * Set up menubar - file, edit, options, and help menus
 */
static GtkWidget*
create_menubar ()
{
	GtkWidget *file_menu;
	GtkWidget *edit_menu;
	GtkWidget *view_menu;
	GtkWidget *tools_menu;
	GtkWidget *help_menu;
	GtkWidget *open_item;
	GtkWidget *print_item;
	GtkWidget *quit_item;
	GtkWidget *cut_item;
	GtkWidget *copy_item;
	GtkWidget *paste_item;
	GtkWidget *delete_item;
	GtkWidget *find_item;
	GtkWidget *find_next_item;
	GtkWidget *zoom_in_item;
	GtkWidget *zoom_out_item;
	GtkWidget *zoom_reset_item;
	GtkWidget *fullscreen_item;
	GtkWidget *settings_item;
	GtkWidget *about_item;
	GtkWidget *file_item;
	GtkWidget *edit_item;
	GtkWidget *view_item;
	GtkWidget *tools_item;
	GtkWidget *help_item;
	
	/* Create menubar */
	menu_bar = gtk_menu_bar_new ();
	gtk_widget_show (menu_bar);
	
	/* Create File, Edit, and Help Menus */
	file_menu = gtk_menu_new ();
	edit_menu = gtk_menu_new ();
	view_menu = gtk_menu_new ();
	tools_menu = gtk_menu_new ();
	help_menu = gtk_menu_new ();
	
	/* Create the menu items (and set icons) */
	open_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_OPEN, accel_group);
	gtk_menu_item_set_label (GTK_MENU_ITEM (open_item), "Open");
	print_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_PRINT, accel_group);
	gtk_menu_item_set_label (GTK_MENU_ITEM (print_item), "Print");
	quit_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_QUIT, accel_group);
	gtk_menu_item_set_label (GTK_MENU_ITEM (quit_item), "Quit");
	cut_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_CUT, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (cut_item), "Cut");
	copy_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_COPY, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (copy_item), "Copy");
	paste_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_PASTE, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (paste_item), "Paste");
	delete_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_DELETE, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (delete_item), "Delete");
	find_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_FIND, accel_group);
	find_next_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_GO_FORWARD, accel_group);
	gtk_menu_item_set_label (GTK_MENU_ITEM (find_item), "Find...");
	zoom_in_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_ZOOM_IN, accel_group);
	gtk_menu_item_set_label (GTK_MENU_ITEM (find_next_item), "Find Next");
	gtk_menu_item_set_label (GTK_MENU_ITEM (zoom_in_item), "Zoom In");
	zoom_out_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_ZOOM_OUT, accel_group);
	gtk_menu_item_set_label (GTK_MENU_ITEM (zoom_out_item), "Zoom Out");
	zoom_reset_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_ZOOM_100, accel_group);
	gtk_menu_item_set_label (GTK_MENU_ITEM (zoom_reset_item), "Reset Zoom");
	fullscreen_item = gtk_check_menu_item_new_with_label ("Fullscreen");
	settings_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_PREFERENCES, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (settings_item), "Settings");
	about_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_ABOUT, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (about_item), "About");
	
	/* Set up accelerators */
	gtk_widget_add_accelerator (open_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator (print_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_p, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator (quit_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	
	gtk_widget_add_accelerator (cut_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_x, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator (copy_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator (paste_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_v, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator (delete_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_m, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator (find_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_f, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator (find_next_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_g, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	
	gtk_widget_add_accelerator (zoom_in_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_plus, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator (zoom_out_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_minus, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator (zoom_reset_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_0, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator (fullscreen_item, "activate", GTK_ACCEL_GROUP (accel_group),  GDK_KEY_F11, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	
	/* Add them to the appropriate menu */
	gtk_menu_append (GTK_MENU (file_menu), open_item);
	gtk_menu_append (GTK_MENU (file_menu), print_item);
	gtk_menu_append (GTK_MENU (file_menu), gtk_separator_menu_item_new ());
	gtk_menu_append (GTK_MENU (file_menu), quit_item);
	
	gtk_menu_append (GTK_MENU (edit_menu), cut_item);
	gtk_menu_append (GTK_MENU (edit_menu), copy_item);
	gtk_menu_append (GTK_MENU (edit_menu), paste_item);
	gtk_menu_append (GTK_MENU (edit_menu), delete_item);
	gtk_menu_append (GTK_MENU (edit_menu), gtk_separator_menu_item_new ());
	gtk_menu_append (GTK_MENU (edit_menu), find_item);
	gtk_menu_append (GTK_MENU (edit_menu), find_next_item);
	
	gtk_menu_append (GTK_MENU (view_menu), zoom_in_item);
	gtk_menu_append (GTK_MENU (view_menu), zoom_out_item);
	gtk_menu_append (GTK_MENU (view_menu), zoom_reset_item);
	gtk_menu_append (GTK_MENU (view_menu), gtk_separator_menu_item_new ());
	gtk_menu_append (GTK_MENU (view_menu), fullscreen_item);
	
	gtk_menu_append (GTK_MENU (tools_menu), settings_item);
	
	gtk_menu_append (GTK_MENU (help_menu), about_item);
	
	/* Attach the callback functions to the activate signal */
	gtk_signal_connect_object (GTK_OBJECT (open_item), "activate", GTK_SIGNAL_FUNC (openfile_cb), (gpointer) "file.open");
	gtk_signal_connect_object (GTK_OBJECT (print_item), "activate", GTK_SIGNAL_FUNC (print_cb), (gpointer) "file.print");
	gtk_signal_connect_object (GTK_OBJECT (quit_item), "activate", GTK_SIGNAL_FUNC (destroy_cb), (gpointer) "file.quit");
	gtk_signal_connect_object (GTK_OBJECT (cut_item), "activate", GTK_SIGNAL_FUNC (cut_cb), (gpointer) "edit.cut");
	gtk_signal_connect_object (GTK_OBJECT (copy_item), "activate", GTK_SIGNAL_FUNC (copy_cb), (gpointer) "edit.copy");
	gtk_signal_connect_object (GTK_OBJECT (paste_item), "activate", GTK_SIGNAL_FUNC (paste_cb), (gpointer) "edit.paste");
	gtk_signal_connect_object (GTK_OBJECT (delete_item), "activate", GTK_SIGNAL_FUNC (delete_cb), (gpointer) "edit.delete");
	gtk_signal_connect_object (GTK_OBJECT (find_item), "activate", GTK_SIGNAL_FUNC (find_dialog_cb), (gpointer) "edit.find");
	gtk_signal_connect_object (GTK_OBJECT (find_next_item), "activate", GTK_SIGNAL_FUNC (find_next_cb), (gpointer) "edit.find-next");
	gtk_signal_connect_object (GTK_OBJECT (zoom_in_item), "activate", GTK_SIGNAL_FUNC (zoom_in_cb), (gpointer) "view.zoom-in");
	gtk_signal_connect_object (GTK_OBJECT (zoom_out_item), "activate", GTK_SIGNAL_FUNC (zoom_out_cb), (gpointer) "view.zoom-out");
	gtk_signal_connect_object (GTK_OBJECT (zoom_reset_item), "activate", GTK_SIGNAL_FUNC (zoom_reset_cb), (gpointer) "view.zoom-reset");
	gtk_signal_connect_object (GTK_OBJECT (fullscreen_item), "activate", GTK_SIGNAL_FUNC (fullscreen_cb), (gpointer) "view.fullscreen");
	gtk_signal_connect_object (GTK_OBJECT (settings_item), "activate", GTK_SIGNAL_FUNC (settings_dialog_cb), (gpointer) "tools.settings");
	gtk_signal_connect_object (GTK_OBJECT (about_item), "activate", GTK_SIGNAL_FUNC (about_cb), (gpointer) "help.about");
	
	/* Show menu items */
	gtk_widget_show (open_item);
	gtk_widget_show (print_item);
	gtk_widget_show (quit_item);
	gtk_widget_show (cut_item);
	gtk_widget_show (copy_item);
	gtk_widget_show (paste_item);
	gtk_widget_show (delete_item);
	gtk_widget_show (find_item);
	gtk_widget_show (find_next_item);
	gtk_widget_show (zoom_in_item);
	gtk_widget_show (zoom_out_item);
	gtk_widget_show (zoom_reset_item);
	gtk_widget_show (fullscreen_item);
	gtk_widget_show (settings_item);
	gtk_widget_show (about_item);
	
	/* Create "File" and "Help" entries in menubar */
	file_item = gtk_menu_item_new_with_label ("File");
	edit_item = gtk_menu_item_new_with_label ("Edit");
	view_item = gtk_menu_item_new_with_label ("View");
	tools_item = gtk_menu_item_new_with_label ("Tools");
	help_item = gtk_menu_item_new_with_label ("Help");
	gtk_widget_show (file_item);
	gtk_widget_show (edit_item);
	gtk_widget_show (view_item);
	gtk_widget_show (tools_item);
	gtk_widget_show (help_item);
	
	/* Associate file_menu with file_item in the menubar */
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (file_item), file_menu);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (edit_item), edit_menu);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (view_item), view_menu);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (tools_item), tools_menu);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (help_item), help_menu);
	
	/* Add file_menu to menu_bar */
	gtk_menu_bar_append (GTK_MENU_BAR (menu_bar), file_item);
	gtk_menu_bar_append (GTK_MENU_BAR (menu_bar), edit_item);
	gtk_menu_bar_append (GTK_MENU_BAR (menu_bar), view_item);
	gtk_menu_bar_append (GTK_MENU_BAR (menu_bar), tools_item);
	gtk_menu_bar_append (GTK_MENU_BAR (menu_bar), help_item);
	
	/* Set up right-click context menu */
	g_signal_connect (GTK_OBJECT (menu_bar), "button-press-event", G_CALLBACK (context_menu_cb), NULL);
	
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
	GtkWidget *toolbar;
	GtkToolItem *item;
	GtkWidget *h_paned;
	
	toolbar = gtk_toolbar_new ();

	gtk_toolbar_set_orientation (GTK_TOOLBAR (toolbar), GTK_ORIENTATION_HORIZONTAL);
	gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_BOTH_HORIZ);

	/* the back button */
	back_button = gtk_tool_button_new_from_stock (GTK_STOCK_GO_BACK);
	gtk_widget_set_tooltip_text (GTK_WIDGET (back_button), "Go back to the previous page");
	g_signal_connect (G_OBJECT (back_button), "clicked", G_CALLBACK (go_back_cb), NULL);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), GTK_TOOL_ITEM (back_button), -1);

	/* The forward button */
	forward_button = gtk_tool_button_new_from_stock (GTK_STOCK_GO_FORWARD);
	gtk_widget_set_tooltip_text (GTK_WIDGET (forward_button), "Go to the next page");
	g_signal_connect (G_OBJECT (forward_button), "clicked", G_CALLBACK (go_forward_cb), NULL);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), GTK_TOOL_ITEM (forward_button), -1);
	
	/* The refresh button */
	refresh_button = gtk_tool_button_new_from_stock (GTK_STOCK_REFRESH);
	gtk_widget_set_tooltip_text (GTK_WIDGET (refresh_button), "Reload the current page");
	g_signal_connect (G_OBJECT (refresh_button), "clicked", G_CALLBACK (refresh_cb), NULL);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), GTK_TOOL_ITEM (refresh_button), -1);

	/* The URL entry */
	uri_entry = gtk_entry_new ();
	g_signal_connect (G_OBJECT (uri_entry), "activate", G_CALLBACK (activate_uri_entry_cb), NULL);
	
	/* The search-engine entry */
	search_engine_entry = gtk_entry_new ();
	gtk_entry_set_icon_from_stock (GTK_ENTRY (search_engine_entry), GTK_ENTRY_ICON_PRIMARY, GTK_STOCK_FILE);
	gtk_entry_set_icon_from_stock (GTK_ENTRY (search_engine_entry), GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_FIND);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY (search_engine_entry), GTK_ENTRY_ICON_PRIMARY, "Choose Search Engine");
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY (search_engine_entry), GTK_ENTRY_ICON_SECONDARY, "Search");
	g_signal_connect (G_OBJECT (search_engine_entry), "activate", G_CALLBACK (activate_search_engine_entry_cb), NULL);
	g_signal_connect (G_OBJECT (search_engine_entry), "icon-press", G_CALLBACK (search_engine_entry_icon_cb), NULL);

	/* Paned widget to hold uri entry and search-engine entry */
	h_paned = gtk_hpaned_new ();
	gtk_paned_pack1 (GTK_PANED (h_paned), uri_entry, TRUE, TRUE);
	gtk_paned_pack2 (GTK_PANED (h_paned), search_engine_entry, FALSE, TRUE);
	
	item = gtk_tool_item_new ();
	gtk_tool_item_set_expand (item, TRUE);
	gtk_container_add (GTK_CONTAINER (item), h_paned);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);
	
	/* The home button */
	item = gtk_tool_button_new_from_stock (GTK_STOCK_HOME);
	gtk_widget_set_tooltip_text (GTK_WIDGET (item), "Go to home page");
	g_signal_connect (G_OBJECT (item), "clicked", G_CALLBACK (home_cb), NULL);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);
	
	/* Set up accelerators (keyboard shortcuts)
	gtk_widget_add_accelerator (GTK_WIDGET (back_button), "activate", accel_group, GDK_s, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE); */
	
	/* Set up right-click context menu */
	g_signal_connect (GTK_OBJECT (toolbar), "button-press-event", G_CALLBACK (context_menu_cb), NULL);

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
	gtk_window_set_icon_name (GTK_WINDOW (window), "web-browser");
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
	g_object_set (G_OBJECT (settings), "user-agent", useragents[0], NULL);
	g_object_set (G_OBJECT (settings), "auto-load-images", loadimages, NULL);
	g_object_set (G_OBJECT (settings), "enable-plugins", enableplugins, NULL);
	g_object_set (G_OBJECT (settings), "enable-scripts", enablescripts, NULL);
	g_object_set (G_OBJECT (settings), "enable-spatial-navigation", enablespatialbrowsing, NULL);
	g_object_set (G_OBJECT (settings), "enable-spell-checking", enablespellchecking, NULL);
	g_object_set (G_OBJECT (settings), "enable-file-access-from-file-uris", TRUE, NULL);
	g_object_set (G_OBJECT (settings), "enable-developer-extras", enableinspector, NULL);
	
	if (hidebackground)
		webkit_web_view_set_transparent(web_view, TRUE);
		
	if (fullcontentzoom)
		webkit_web_view_set_full_content_zoom(web_view, TRUE);
	
	/* Apply settings */
	webkit_web_view_set_settings (WEBKIT_WEB_VIEW(web_view), settings);
}

/*
 * Main function of program
 */
int
main (int argc, char* argv[])
{	
	GtkWidget *vbox;
	
	gtk_init (&argc, &argv);
	
	if (argc > 1)
		if (argv[1][1] == 'v')
		{
			printf ("surf-"VERSION", 2014 David Luco\n");
			return 0;
		}
		
	main_window = create_window ();
	
	accel_group = gtk_accel_group_new ();
	gtk_window_add_accel_group (GTK_WINDOW (main_window), accel_group);
	
	vbox = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), create_menubar (), FALSE, FALSE, 0);
	main_toolbar = create_toolbar ();
	gtk_box_pack_start (GTK_BOX (vbox), main_toolbar, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), create_browser (), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), create_statusbar (), FALSE, FALSE, 0);

	gtk_container_add (GTK_CONTAINER (main_window), vbox);
	
	find_buffer = gtk_entry_buffer_new (NULL, -1);
	set_settings ();
	gchar* uri = (gchar*) (argc > 1 ? argv[1] : home_page);
	webkit_web_view_load_uri (web_view, uri);

	gtk_widget_grab_focus (GTK_WIDGET (web_view));
	gtk_widget_show_all (main_window);
	gtk_main ();

	return 0;
}
