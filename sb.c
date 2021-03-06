/*
 * sb - simple browser
 * 
 * David Luco - <dluco11 at gmail dot com>
 * 
 * See LICENSE file for copyright and license details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include "config.h"

static GtkWidget* main_window;
static GtkWidget* main_book;
static GtkWidget* main_menu_bar;

static GtkWidget* main_toolbar;
static GtkToolItem* back_button;
static GtkToolItem* forward_button;
static GtkToolItem* refresh_button;
static GtkWidget* uri_entry;
static GtkEntryBuffer* search_buffer;
static GtkWidget* search_engine_entry;

static GtkStatusbar* main_statusbar;
static WebKitWebView* web_view;
static gchar* main_title;
static gint load_progress;
static guint status_context_id;

static gboolean fullscreen = FALSE;

static int user_agent_current = 0;
static char* useragents[] = {
	"Mozilla/5.0 (X11; U; Unix; en-US) AppleWebKit/537.15 (KHTML, like Gecko) Chrome/24.0.1295.0 Safari/537.15 sb/0.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:31.0) Gecko/20100101 Firefox/31.0",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_2) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/36.0.1944.0 Safari/537.36",
	"Mozilla/5.0 (compatible; MSIE 10.6; Windows NT 6.1; Trident/5.0; InfoPath.2; SLCC1; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 2.0.50727) 3gpp-gba UNTRUSTED/1.0",
	"Mozilla/5.0 (Linux; U; Android 4.0.3; ko-kr; LG-L160L Build/IML74K) AppleWebkit/534.30 (KHTML, like Gecko) Version/4.0 Mobile Safari/534.30",
};

typedef struct Client {
	GtkWidget *vbox, *scroll, *pane;
	WebKitWebView* view;
	WebKitWebInspector *inspector;
	const char *uri;
	gint progress;
	gboolean zoomed, isinspecting;
} Client;

static Client* current_client = NULL;

typedef struct engine {
	char* name;
	char* url;
} engine;

static int search_engine_current = 0;
static engine search_engines[] = {
	{"Google", "https://www.google.ca/#q="},
	{"Duck Duck Go", "https://duckduckgo.com/?q="}
};

static void activate_uri_entry_cb (GtkWidget*, gpointer);
static void activate_search_engine_entry_cb (GtkWidget*, gpointer);
static void choose_search_engine_dialog ();
static void search_engine_entry_icon_cb (GtkEntry*, GtkEntryIconPosition, GdkEvent*, gpointer);
static void update_title (GtkWindow*);
static void link_hover_cb (WebKitWebView*, const gchar*, const gchar*, gpointer);
static gboolean decide_download_cb (WebKitWebView*, WebKitWebFrame*, WebKitNetworkRequest*, gchar*,  WebKitWebPolicyDecision*, gpointer);
static gboolean init_download_cb (WebKitWebView*, WebKitDownload*, gpointer);


static Client* create_new_client ();

/*
 * Callback to exit program
 */
static void
destroy_cb (GtkWidget* widget, gpointer data)
{
	gtk_main_quit ();
}

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
	GtkWidget* dialog = gtk_dialog_new_with_buttons ("Engine...",
													GTK_WINDOW (main_window),
													GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
													GTK_STOCK_CANCEL,
													GTK_RESPONSE_REJECT,
													GTK_STOCK_OK,
													GTK_RESPONSE_ACCEPT,
													NULL);
													
	GtkWidget* vbox = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	
	GtkWidget* combo_box = gtk_combo_box_text_new ();
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
search_engine_entry_icon_cb (GtkEntry* entry, GtkEntryIconPosition icon_pos, GdkEvent* event, gpointer data)
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
static void
tab_switched_cb (GtkNotebook* notebook, gpointer page, guint page_num, gpointer data)
{
	web_view = (WebKitWebView*)gtk_paned_get_child1 (GTK_PANED ( (gtk_notebook_get_nth_page (GTK_NOTEBOOK (main_book), page_num))));
	WebKitWebFrame* frame = webkit_web_view_get_main_frame (web_view);
	const gchar* uri = webkit_web_frame_get_uri (frame);
	if (uri)
		gtk_entry_set_text (GTK_ENTRY (uri_entry), uri);
}*/

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

static void
notebook_tab_close_clicked_cb (GtkButton *button, gpointer data)
{
	/* Close browser if only one tab open */
	if (gtk_notebook_get_n_pages (GTK_NOTEBOOK (main_book)) == 1)
	{
		destroy_cb (main_window, NULL);
	}
	gint page_num = gtk_notebook_page_num (GTK_NOTEBOOK (main_book), GTK_WIDGET (data));
	gtk_notebook_remove_page (GTK_NOTEBOOK (main_book), page_num);
}

static void
notebook_tab_close_button_style_set (GtkWidget *btn, GtkRcStyle *prev_style, gpointer data)
{
	gint w, h;

	gtk_icon_size_lookup_for_settings(gtk_widget_get_settings(btn), GTK_ICON_SIZE_MENU, &w, &h);
	gtk_widget_set_size_request(btn, w + 2, h + 2);
}

static GtkWidget*
create_tab_label (Client *c, const gchar *label_text)
{
	GtkWidget *hbox, *label, *button, *image, *align;
	
	hbox = gtk_hbox_new (FALSE, 2);
	
	label = gtk_label_new (label_text);
	
	button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_button_set_focus_on_click (GTK_BUTTON (button), FALSE);
	gtk_widget_set_name (button, "sb-close-tab-button");
		
	image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
	gtk_container_add (GTK_CONTAINER (button), image);
	
	align = gtk_alignment_new (1.0, 0.5, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (align), button);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), align, TRUE, TRUE, 0);
	
	g_signal_connect (button, "clicked", G_CALLBACK (notebook_tab_close_clicked_cb), c->pane);
	g_signal_connect (button, "style-set", G_CALLBACK (notebook_tab_close_button_style_set), NULL);
	
	gtk_widget_show_all (hbox);
	
	return hbox;
}

static WebKitWebView*
create_new_tab (WebKitWebView  *v, WebKitWebFrame *f, Client *c)
{
	Client* n;
	GtkWidget* hbox;
	const gchar* label_text;
	
	n = create_new_client ();
		
	label_text = webkit_web_frame_get_name (f);
	
	hbox = create_tab_label (n, label_text);
	
	gtk_notebook_append_page (GTK_NOTEBOOK (main_book), n->pane, hbox);
	gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (main_book), n->pane, TRUE);
	gtk_widget_show_all (n->pane);
	if (!openinbackground)
		gtk_notebook_set_current_page (GTK_NOTEBOOK(main_book), gtk_notebook_get_n_pages (GTK_NOTEBOOK(main_book)) - 1);
	current_client = n;
	return n->view;
}

static WebKitWebView*
inspector_new (WebKitWebInspector* i, WebKitWebView* v, Client* c)
{
	return WEBKIT_WEB_VIEW (webkit_web_view_new ());
}

static gboolean
inspector_show (WebKitWebInspector *i, Client *c)
{
	WebKitWebView *w;

	if (c->isinspecting)
		return false;

	w = webkit_web_inspector_get_web_view (i);
	gtk_paned_pack2 (GTK_PANED (c->pane), GTK_WIDGET (w), TRUE, TRUE);
	gtk_widget_show(GTK_WIDGET (w));
	c->isinspecting = true;

	return true;
}

static gboolean
inspector_close (WebKitWebInspector *i, Client *c)
{
	GtkWidget *w;

	if (!c->isinspecting)
		return false;

	w = GTK_WIDGET (webkit_web_inspector_get_web_view(i));
	gtk_widget_hide (w);
	gtk_widget_destroy (w);
	c->isinspecting = false;

	return true;
}

static void
inspector_finished (WebKitWebInspector *i, Client *c)
{
	g_free (c->inspector);
}

static void
inspector (GtkCheckMenuItem *checkmenuitem, gpointer data)
{
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (checkmenuitem)))
	{
		inspector_close (current_client->inspector, current_client);
	} else
	{
		inspector_new (current_client->inspector, web_view, current_client);
	}
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
 *
static void
progress_change_cb (WebKitWebView* web_view, gint progress, gpointer data)
{
	load_progress = progress;
	update_title (GTK_WINDOW (main_window));
}*/

static void
progress_change_cb (WebKitWebView *view, GParamSpec *pspec, Client *c)
{
	c->progress = webkit_web_view_get_progress(c->view) * 100;
}

/*
 * Callback for a change in the load status of a web view
 */
static void
load_status_change_cb (WebKitWebView* web_view, gpointer data)
{
	WebKitWebFrame* frame;
	GtkWidget* hbox;
	const gchar *uri, *label;
	
	switch (webkit_web_view_get_load_status (web_view))
	{
		case WEBKIT_LOAD_COMMITTED:
			/* Update uri in entry-bar */
			frame = webkit_web_view_get_main_frame (web_view);
			uri = webkit_web_frame_get_uri (frame);
			label = webkit_web_frame_get_name (frame);
			if (uri)
				gtk_entry_set_text (GTK_ENTRY (uri_entry), uri);
			/* Update tab-label */
			hbox = create_tab_label (current_client, label);
			gtk_notebook_set_tab_label (GTK_NOTEBOOK (main_book), current_client->pane, hbox);
			break;
		case WEBKIT_LOAD_FINISHED:
			break;
		default:
			break;
	}
	
	/* Update buttons - back, forward */
	gtk_widget_set_sensitive (GTK_WIDGET (back_button), webkit_web_view_can_go_back (web_view));
	gtk_widget_set_sensitive (GTK_WIDGET (forward_button), webkit_web_view_can_go_forward (web_view));
}

/*
 * Callback for file.open - open file-chooser dialog and open selected file in webview
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
		gchar *filename;
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
	webkit_web_view_cut_clipboard (web_view);
}

/*
 * Callback for edit.copy - copy current selection
 */
static void
copy_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_copy_clipboard (web_view);
}

/*
 * Callback for edit.paste - paste current selection
 */
static void
paste_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_paste_clipboard (web_view);
}

/* 
 * Callback for edit.delete - delete current selection
 */
static void
delete_cb (GtkWidget* widget, gpointer data)
{
	webkit_web_view_delete_selection (web_view);
}

/*
 * Dialog to search for text in current web-view
 */
static void
find_dialog_cb (GtkWidget* widget, gpointer data)
{
	GtkWidget* dialog = gtk_dialog_new_with_buttons ("Find",
													GTK_WINDOW (main_window),
													GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
													GTK_STOCK_CANCEL,
													GTK_RESPONSE_REJECT,
													GTK_STOCK_FIND,
													GTK_RESPONSE_ACCEPT,
													NULL);
	
	/* Get area for entry and toggles, and get the find button */
	GtkWidget* content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	GtkWidget* find_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
	
	/* Set "Find" button as default button */
	gtk_widget_set_can_default (find_button, TRUE);
	gtk_widget_grab_default (find_button);
	
	/* Create a hbox to hold label and entry */
	GtkWidget* hbox = gtk_hbox_new (FALSE, 0);
	GtkWidget* label = gtk_label_new ("Find what:");
	
	/* Set up entry - entry activation activates the find button as well */
	GtkWidget* find_entry = gtk_entry_new_with_buffer (GTK_ENTRY_BUFFER (search_buffer));
	gtk_entry_set_activates_default (GTK_ENTRY (find_entry), TRUE);
	
	/* Pack label and entry into hbox */
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 5);
	gtk_box_pack_start (GTK_BOX (hbox), find_entry, FALSE, FALSE, 5);
	
	/* Pack hbox into the content area */
	gtk_box_pack_start (GTK_BOX (content_area), hbox, FALSE, FALSE, 5);
	
	/* Set up toggles for matching case, searching forward, and wrapping the search */
	GtkWidget* case_button = gtk_check_button_new_with_label ("Match case");
	gtk_box_pack_start (GTK_BOX (content_area), case_button, FALSE, FALSE, 5);
	
	GtkWidget* find_forward_button = gtk_check_button_new_with_label ("Search forward");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (find_forward_button), TRUE);
	gtk_box_pack_start (GTK_BOX (content_area), find_forward_button, FALSE, FALSE, 5);
	
	GtkWidget* wrap_button = gtk_check_button_new_with_label ("Wrap search");
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
	GtkWidget* dialog = gtk_dialog_new_with_buttons ("sb Settings",
													GTK_WINDOW (main_window),
													GTK_DIALOG_DESTROY_WITH_PARENT,
													GTK_STOCK_CANCEL,
													GTK_RESPONSE_REJECT,
													GTK_STOCK_OK,
													GTK_RESPONSE_ACCEPT,
													NULL);
	
	GtkWidget* vbox = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	WebKitWebSettings* settings = webkit_web_view_get_settings (web_view);
	gboolean isactive = FALSE;
	
	/* Check-button to control smooth-scrolling */
	GtkWidget* smooth_scrolling_button = gtk_check_button_new_with_label ("Enable smooth-scrolling");
	g_object_get (G_OBJECT (settings), "enable-smooth-scrolling", &isactive, NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (smooth_scrolling_button), isactive);
	gtk_box_pack_start (GTK_BOX (vbox), smooth_scrolling_button, TRUE, TRUE, 2);
	gtk_widget_show (smooth_scrolling_button);
	
	/* Check-button to control private browsing */
	GtkWidget* private_browsing_button = gtk_check_button_new_with_label ("Enable private browsing");
	g_object_get (G_OBJECT (settings), "enable-private-browsing", &isactive, NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (private_browsing_button), isactive);
	gtk_box_pack_start (GTK_BOX (vbox), private_browsing_button, TRUE, TRUE, 2);
	gtk_widget_show (private_browsing_button);
	
	/* Combox-box to choose the useragent to use */
	GtkWidget* combo_box = gtk_combo_box_text_new ();
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
 * Apply default settings to web-view
 */
static void
set_settings (WebKitWebView* web_view)
{
	WebKitWebSettings *settings = webkit_web_settings_new ();
	
	/* Apply default settings from config.h */
	g_object_set (G_OBJECT (settings), "user-agent", useragents[user_agent_current], NULL);
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
	webkit_web_view_set_settings (WEBKIT_WEB_VIEW (web_view), settings);
}

/*
 * Set up menubar - file, edit, options, and help menus
 */
static GtkWidget*
create_menubar ()
{
	/* Create menubar */
	GtkWidget* menu_bar = gtk_menu_bar_new ();
	gtk_widget_show (menu_bar);
	
	/* Create File, Edit, and Help Menus */
	GtkWidget* file_menu = gtk_menu_new ();
	GtkWidget* edit_menu = gtk_menu_new ();
	GtkWidget* view_menu = gtk_menu_new ();
	GtkWidget* tools_menu = gtk_menu_new ();
	GtkWidget* help_menu = gtk_menu_new ();
	
	/* Create the menu items (and set icons) */
	GtkWidget* open_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_OPEN, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (open_item), "Open");
	GtkWidget* print_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_PRINT, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (print_item), "Print");
	GtkWidget* quit_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_QUIT, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (quit_item), "Quit");
	GtkWidget* cut_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_CUT, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (cut_item), "Cut");
	GtkWidget* copy_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_COPY, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (copy_item), "Copy");
	GtkWidget* paste_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_PASTE, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (paste_item), "Paste");
	GtkWidget* delete_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_DELETE, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (delete_item), "Delete");
	GtkWidget* find_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_FIND, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (find_item), "Find...");
	GtkWidget* zoom_in_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_ZOOM_IN, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (zoom_in_item), "Zoom In");
	GtkWidget* zoom_out_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_ZOOM_OUT, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (zoom_out_item), "Zoom Out");
	GtkWidget* zoom_reset_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_ZOOM_100, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (zoom_reset_item), "Reset Zoom");
	GtkWidget* fullscreen_item = gtk_check_menu_item_new_with_label ("Fullscreen");
	GtkWidget* settings_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_PREFERENCES, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (settings_item), "Settings");
	GtkWidget* inspector_item = gtk_check_menu_item_new_with_label ("Inspector");
	GtkWidget* about_item = gtk_image_menu_item_new_from_stock (GTK_STOCK_ABOUT, NULL);
	gtk_menu_item_set_label (GTK_MENU_ITEM (about_item), "About");
	
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
	
	gtk_menu_append (GTK_MENU (view_menu), zoom_in_item);
	gtk_menu_append (GTK_MENU (view_menu), zoom_out_item);
	gtk_menu_append (GTK_MENU (view_menu), zoom_reset_item);
	gtk_menu_append (GTK_MENU (view_menu), fullscreen_item);
	
	gtk_menu_append (GTK_MENU (tools_menu), settings_item);
	if (enableinspector)
		gtk_menu_append (GTK_MENU (tools_menu), inspector_item);
	
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
	gtk_signal_connect_object (GTK_OBJECT (zoom_in_item), "activate", GTK_SIGNAL_FUNC (zoom_in_cb), (gpointer) "view.zoom-in");
	gtk_signal_connect_object (GTK_OBJECT (zoom_out_item), "activate", GTK_SIGNAL_FUNC (zoom_out_cb), (gpointer) "view.zoom-out");
	gtk_signal_connect_object (GTK_OBJECT (zoom_reset_item), "activate", GTK_SIGNAL_FUNC (zoom_reset_cb), (gpointer) "view.zoom-reset");
	gtk_signal_connect_object (GTK_OBJECT (fullscreen_item), "activate", GTK_SIGNAL_FUNC (fullscreen_cb), (gpointer) "view.fullscreen");
	gtk_signal_connect_object (GTK_OBJECT (settings_item), "activate", GTK_SIGNAL_FUNC (settings_dialog_cb), (gpointer) "tools.settings");
	if (enableinspector)
		gtk_signal_connect_object (GTK_OBJECT (inspector_item), "activate", GTK_SIGNAL_FUNC (inspector), (gpointer) "tools.inspector");
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
	gtk_widget_show (zoom_in_item);
	gtk_widget_show (zoom_out_item);
	gtk_widget_show (zoom_reset_item);
	gtk_widget_show (fullscreen_item);
	gtk_widget_show (settings_item);
	if (enableinspector)
		gtk_widget_show (inspector_item);
	gtk_widget_show (about_item);
	
	/* Create "File" and "Help" entries in menubar */
	GtkWidget* file_item = gtk_menu_item_new_with_label ("File");
	GtkWidget* edit_item = gtk_menu_item_new_with_label ("Edit");
	GtkWidget* view_item = gtk_menu_item_new_with_label ("View");
	GtkWidget* tools_item = gtk_menu_item_new_with_label ("Tools");
	GtkWidget* help_item = gtk_menu_item_new_with_label ("Help");
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
	
	GtkToolItem* item;

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
	GtkWidget* h_paned = gtk_hpaned_new ();
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

	return toolbar;
}

static Client*
create_new_client ()
{
	Client* c;
	
	if (!(c = calloc(1, sizeof (Client))))
		fprintf(stderr, "Cannot allocate memory for client\n");
	
	/* Pane, vobx, scrolled-window */
	c->pane = gtk_vpaned_new();
	c->vbox = gtk_vbox_new (FALSE, 0);
	c->scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (c->scroll), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	
	/* Setup web-view */
	c->view = WEBKIT_WEB_VIEW (webkit_web_view_new ());
	
	g_signal_connect (G_OBJECT (c->view), "title-changed", G_CALLBACK (title_change_cb), c);
	g_signal_connect (G_OBJECT (c->view), "notify::progress", G_CALLBACK (progress_change_cb), c);
	g_signal_connect (G_OBJECT (c->view), "notify::load-status", G_CALLBACK (load_status_change_cb), c);
	g_signal_connect (G_OBJECT (c->view), "hovering-over-link", G_CALLBACK (link_hover_cb), c);
	g_signal_connect (G_OBJECT (c->view), "mime-type-policy-decision-requested", G_CALLBACK (decide_download_cb), c);
	g_signal_connect (G_OBJECT (c->view), "download-requested", G_CALLBACK (init_download_cb), c);
	g_signal_connect (G_OBJECT (c->view), "create-web-view", G_CALLBACK (create_new_tab), c);
	
	/* Settings */
	set_settings (c->view);
	
	/* Arrangement of containers */
	gtk_container_add (GTK_CONTAINER (c->scroll), GTK_WIDGET (c->view));
	gtk_container_add (GTK_CONTAINER (c->vbox), c->scroll);
	gtk_paned_pack1 (GTK_PANED (c->pane), c->vbox, TRUE, TRUE);
	/*gtk_notebook_append_page (GTK_NOTEBOOK (main_book), c->pane, NULL);*/
	
	if(enableinspector)
	{
		c->inspector = WEBKIT_WEB_INSPECTOR (webkit_web_view_get_inspector(c->view));
		
		g_signal_connect (G_OBJECT (c->inspector), "inspect-web-view", G_CALLBACK (inspector_new), c);
		g_signal_connect (G_OBJECT (c->inspector), "show-window", G_CALLBACK (inspector_show), c);
		g_signal_connect (G_OBJECT (c->inspector), "close-window", G_CALLBACK (inspector_close), c);
		g_signal_connect (G_OBJECT (c->inspector), "finished", G_CALLBACK (inspector_finished), c);
		
		c->isinspecting = FALSE;
	}
	
	return c;
}

static GtkWidget*
create_notebook ()
{
	GtkWidget* notebook = gtk_notebook_new ();
	gtk_notebook_popup_enable (GTK_NOTEBOOK (notebook));
	/*g_signal_connect (G_OBJECT (notebook), "switch-page", G_CALLBACK (tab_switched_cb), NULL);*/
	
	return notebook;
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
 * Main function of program
 */
int
main (int argc, char* argv[])
{	
	gtk_init (&argc, &argv);
	
	if (argc > 1)
		if (argv[1][1] == 'v')
		{
			printf ("surf-"VERSION", 2014 David Luco\n");
			return 0;
		}
	
	/* Create GtkNotebook to hold web page tabs */
	main_book = create_notebook ();
	GtkWidget* vbox = gtk_vbox_new (FALSE, 0);
	main_menu_bar = create_menubar ();
	gtk_box_pack_start (GTK_BOX (vbox), main_menu_bar, FALSE, FALSE, 0);
	main_toolbar = create_toolbar ();
	gtk_box_pack_start (GTK_BOX (vbox), main_toolbar, FALSE, FALSE, 0);
	Client* c = create_new_client ();
	web_view = c->view;
	gtk_notebook_append_page (GTK_NOTEBOOK (main_book), c->pane, NULL);
	gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (main_book), c->pane, TRUE);
	current_client = c;
	gtk_box_pack_start (GTK_BOX (vbox), main_book, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), create_statusbar (), FALSE, FALSE, 0);
	
	main_window = create_window ();
	gtk_container_add (GTK_CONTAINER (main_window), vbox);
	
	search_buffer = gtk_entry_buffer_new (NULL, -1);
	gchar* uri = (gchar*) (argc > 1 ? argv[1] : home_page);
	
	/* Get current web-view from notebook 
	web_view = (WebKitWebView*)gtk_bin_get_child (GTK_BIN (gtk_notebook_get_nth_page (GTK_NOTEBOOK (main_book), gtk_notebook_get_current_page (GTK_NOTEBOOK (main_book)))));
	*/
	webkit_web_view_load_uri (web_view, uri);

	gtk_widget_grab_focus (GTK_WIDGET (web_view));
	gtk_widget_show_all (main_window);
	gtk_main ();

	return 0;
}
