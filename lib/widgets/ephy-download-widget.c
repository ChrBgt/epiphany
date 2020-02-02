/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2011, 2015 Igalia S.L.
 *
 *  This file is part of Epiphany.
 *
 *  Epiphany is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Epiphany is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Epiphany.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "ephy-download-widget.h"

#include "ephy-debug.h"
#include "ephy-downloads-manager.h"
#include "ephy-embed-shell.h"
#include "ephy-flatpak-utils.h"
#include "ephy-uri-helpers.h"

#include <glib/gi18n.h>
#include <webkit2/webkit2.h>

//CHB
#define MAX_NOF_WIDGETS_ACTIVE 4
static int nof_widgets_active= 0;
//eof CHB

struct _EphyDownloadWidget {
  GtkGrid parent_instance;  
/* struct _EphyDownloadWidgetPrivate CHB
{*/
  EphyDownload *download;

  GtkWidget *filename;
  GtkWidget *status;
  GtkWidget *icon;
  GtkWidget *progress;
  GtkWidget *action_button;
  gint     timeout_id;//CHB
};

G_DEFINE_TYPE (EphyDownloadWidget, ephy_download_widget, GTK_TYPE_GRID)

enum {
  PROP_0,
  PROP_DOWNLOAD,
  LAST_PROP
};

static GParamSpec *obj_properties[LAST_PROP];

static char *
get_destination_basename_from_download (EphyDownload *ephy_download)
{
  WebKitDownload *download;
  const char *dest;
  char *basename;
  char *decoded;

  download = ephy_download_get_webkit_download (ephy_download);
  dest = webkit_download_get_destination (download);
  if (!dest)
    return NULL;

  decoded = ephy_uri_decode (dest);
  basename = g_filename_display_basename (decoded);
  g_free (decoded);

  return basename;
}

/* modified from telepathy-account-widgets/tpaw-time.c */
static gchar *
duration_to_string (guint seconds)
{
  if (seconds < 60) {
    return g_strdup_printf (ngettext ("%d second left",
                                      "%d seconds left", seconds), seconds);
  } else if (seconds < (60 * 60)) {
    seconds /= 60;
    return g_strdup_printf (ngettext ("%d minute left",
                                      "%d minutes left", seconds), seconds);
  } else if (seconds < (60 * 60 * 24)) {
    seconds /= 60 * 60;
    return g_strdup_printf (ngettext ("%d hour left",
                                      "%d hours left", seconds), seconds);
  } else if (seconds < (60 * 60 * 24 * 7)) {
    seconds /= 60 * 60 * 24;
    return g_strdup_printf (ngettext ("%d day left",
                                      "%d days left", seconds), seconds);
  } else if (seconds < (60 * 60 * 24 * 30)) {
    seconds /= 60 * 60 * 24 * 7;
    return g_strdup_printf (ngettext ("%d week left",
                                      "%d weeks left", seconds), seconds);
  } else {
    seconds /= 60 * 60 * 24 * 30;
    return g_strdup_printf (ngettext ("%d month left",
                                      "%d months left", seconds), seconds);
  }
}

static gdouble
get_remaining_time (guint64 content_length,
                    guint64 received_length,
                    gdouble elapsed_time)
{
  gdouble remaining_time;
  gdouble per_byte_time;

  per_byte_time = elapsed_time / received_length;
  remaining_time = per_byte_time * (content_length - received_length);

  return remaining_time;
}

/* CHB check tobedeleted
//CHB
static gboolean
delayed_widget_removal(EphyDownloadWidget *widget)
{
  nof_widgets_active--;
  gtk_widget_destroy (GTK_WIDGET (widget));

  return G_SOURCE_REMOVE;
}
//eof CHB

static void
download_clicked_cb (GtkButton *button,
                     EphyDownloadWidget *widget)
{
  //CHB
  if(widget->priv->timeout_id) g_source_remove(widget->priv->timeout_id);
  gtk_label_set_text (widget->priv->text, "Click download button to the right!");
  widget->priv->timeout_id = g_timeout_add(3000, (GSourceFunc)delayed_widget_removal, widget);
  //eof CHB
}
*/

static void
update_download_icon (EphyDownloadWidget *widget)
{
  GIcon *icon;
  const char *content_type;

  content_type = ephy_download_get_content_type (widget->download);
  if (content_type) {
    icon = g_content_type_get_symbolic_icon (content_type);
    /* g_content_type_get_symbolic_icon() always creates a GThemedIcon, but we check it
     * here just in case that changes in GLib eventually.
     */
    if (G_IS_THEMED_ICON (icon)) {
      /* Ensure we always fallback to package-x-generic-symbolic if all other icons are
       * missing in the theme.
       */
      g_themed_icon_append_name (G_THEMED_ICON (icon), "package-x-generic-symbolic");
    }
  } else
    icon = g_icon_new_for_string ("package-x-generic-symbolic", NULL);

  gtk_image_set_from_gicon (GTK_IMAGE (widget->icon), icon, GTK_ICON_SIZE_MENU);
  g_object_unref (icon);
}

static void
update_download_destination (EphyDownloadWidget *widget)
{
  char *dest;

  dest = get_destination_basename_from_download (widget->download);
  if (!dest)
    return;

  gtk_label_set_label (GTK_LABEL (widget->filename), dest);

  g_free (dest);
}

static void
update_status_label (EphyDownloadWidget *widget,
                     const char         *download_label)
{
  char *markup;

  markup = g_markup_printf_escaped ("<span size='small'>%s</span>", download_label);
  gtk_label_set_markup (GTK_LABEL (widget->status), markup);
  g_free (markup);
}

static void
download_progress_cb (WebKitDownload     *download,
                      GParamSpec         *pspec,
                      EphyDownloadWidget *widget)
{
  gdouble progress;
  WebKitURIResponse *response;
  guint64 content_length;
  guint64 received_length;
  char *download_label = NULL;

  if (!webkit_download_get_destination (download))
    return;

  progress = webkit_download_get_estimated_progress (download);
  response = webkit_download_get_response (download);
  content_length = webkit_uri_response_get_content_length (response);
  received_length = webkit_download_get_received_data_length (download);

  if (content_length > 0 && received_length > 0) {
    gdouble time;
    char *remaining;
    char *received;
    char *total;

    received = g_format_size (received_length);
    total = g_format_size (content_length);

    time = get_remaining_time (content_length, received_length,
                               webkit_download_get_elapsed_time (download));
    remaining = duration_to_string ((guint)time);
    download_label = g_strdup_printf ("%s / %s — %s", received, total, remaining);
    g_free (received);
    g_free (total);
    g_free (remaining);

    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget->progress),
                                   progress);
  } else if (received_length > 0) {
    download_label = g_format_size (received_length);
    gtk_progress_bar_pulse (GTK_PROGRESS_BAR (widget->progress));
  }

  if (download_label) {
    update_status_label (widget, download_label);
    g_free (download_label);
  }
}

static void
download_finished_cb (EphyDownload       *download,
                      EphyDownloadWidget *widget)
{
  gtk_widget_hide (widget->progress);
  update_status_label (widget, _("Finished"));
  gtk_image_set_from_icon_name (GTK_IMAGE (gtk_button_get_image (GTK_BUTTON (widget->action_button))),
                                "folder-open-symbolic",
                                GTK_ICON_SIZE_MENU);
}

static void
download_failed_cb (EphyDownload       *download,
                    GError             *error,
                    EphyDownloadWidget *widget)
{
/*CHB TODO check tobedeleted
  widget->priv->finished = TRUE;
  update_popup_menu (widget);
  
  //CHB
  if(nof_widgets_active > MAX_NOF_WIDGETS_ACTIVE)
    update_download_label_and_tooltip (widget, _("Download finished - click to close!"));
  else //eof CHB
    update_download_label_and_tooltip (widget, _("Download finished")); //CHB "Download" in string added
  widget_attention_needed (widget);
}

static void
widget_failed_cb (WebKitDownload *download,
                  GError *error,
                  EphyDownloadWidget *widget)
{
*/
  char *error_msg;

  g_signal_handlers_disconnect_by_func (download, download_progress_cb, widget);

  gtk_widget_hide (widget->progress);

  error_msg = g_strdup_printf (_("Error downloading: %s"), error->message);
  update_status_label (widget, error_msg);
  g_free (error_msg);
  gtk_image_set_from_icon_name (GTK_IMAGE (gtk_button_get_image (GTK_BUTTON (widget->action_button))),
                                "list-remove-symbolic",
                                GTK_ICON_SIZE_MENU);
}

static void
download_content_type_changed_cb (EphyDownload       *download,
                                  GParamSpec         *spec,
                                  EphyDownloadWidget *widget)
{
  update_download_icon (widget);
/*CHB TODO check tobedeleted
  GtkWidget *item;
  GtkWidget *menu;
  char *basename, *name;
  WebKitDownload *download;
  const char *dest;

  download = ephy_download_get_webkit_download (widget->priv->download);
  dest = webkit_download_get_destination (download);
  if (dest == NULL)
    return;

  basename = g_filename_display_basename (dest);
  name = ephy_uri_safe_unescape (basename);

  menu = gtk_menu_new ();
  gtk_widget_set_halign (menu, GTK_ALIGN_END);

  item = gtk_menu_item_new_with_label (name);
  gtk_widget_set_sensitive (item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_free (basename);
  g_free (name);

  widget->priv->cancel_menuitem = item = gtk_menu_item_new_with_label (_("Cancel"));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate",
                    G_CALLBACK (cancel_activate_cb), widget);

  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  widget->priv->open_menuitem = item = gtk_menu_item_new_with_label (_("Open"));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate",
                    G_CALLBACK (open_activate_cb), widget);

  widget->priv->show_folder_menuitem = item = gtk_menu_item_new_with_label (_("Show in folder"));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate",
                    G_CALLBACK (folder_activate_cb), widget);

  update_popup_menu (widget);

  gtk_widget_show_all (menu);

  gtk_menu_button_set_popup (GTK_MENU_BUTTON (widget->priv->menu_button), menu);
}

static void
widget_destination_changed_cb (WebKitDownload *download,
                               GParamSpec *pspec,
                               EphyDownloadWidget *widget)
{
  update_download_destination (widget);
  //add_popup_menu (widget); CHB
*/
}

static void
widget_action_button_clicked_cb (EphyDownloadWidget *widget)
{
  if (ephy_download_is_active (widget->download)) {
    WebKitDownload *download;

    download = ephy_download_get_webkit_download (widget->download);
    g_signal_handlers_disconnect_matched (download, G_SIGNAL_MATCH_DATA, 0, 0,
                                          NULL, NULL, widget);
    g_signal_handlers_disconnect_matched (widget->download, G_SIGNAL_MATCH_DATA, 0, 0,
                                          NULL, NULL, widget);
    update_status_label (widget, _("Cancelling…"));
    gtk_widget_set_sensitive (widget->action_button, FALSE);

    ephy_download_cancel (widget->download);
  } else if (ephy_download_failed (widget->download, NULL)) {
    EphyDownloadsManager *manager;

    manager = ephy_embed_shell_get_downloads_manager (ephy_embed_shell_get_default ());
    ephy_downloads_manager_remove_download (manager, widget->download);
  } else {
    ephy_download_do_download_action (widget->download,
                                      ephy_is_running_inside_flatpak () ? EPHY_DOWNLOAD_ACTION_OPEN : EPHY_DOWNLOAD_ACTION_BROWSE_TO,
                                      gtk_get_current_event_time ());
  }
}

static void
download_destination_changed_cb (WebKitDownload     *download,
                                 GParamSpec         *pspec,
                                 EphyDownloadWidget *widget)
{
  update_download_destination (widget);
}

static void
ephy_download_widget_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  EphyDownloadWidget *widget;

  widget = EPHY_DOWNLOAD_WIDGET (object);

  switch (property_id) {
    case PROP_DOWNLOAD:
      g_value_set_object (value, ephy_download_widget_get_download (widget));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
ephy_download_widget_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  EphyDownloadWidget *widget;
  widget = EPHY_DOWNLOAD_WIDGET (object);

  switch (property_id) {
    case PROP_DOWNLOAD:
      widget->download = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
ephy_download_widget_dispose (GObject *object)
{
  EphyDownloadWidget *widget;

  LOG ("EphyDownloadWidget %p dispose", object);

  widget = EPHY_DOWNLOAD_WIDGET (object);

  if (widget->download != NULL) {
    WebKitDownload *download = ephy_download_get_webkit_download (widget->download);

    g_signal_handlers_disconnect_matched (download, G_SIGNAL_MATCH_DATA, 0, 0,
                                          NULL, NULL, widget);
    g_signal_handlers_disconnect_matched (widget->download, G_SIGNAL_MATCH_DATA, 0, 0,
                                          NULL, NULL, widget);
    g_object_unref (widget->download);
    widget->download = NULL;
  }

  G_OBJECT_CLASS (ephy_download_widget_parent_class)->dispose (object);
}

static void
ephy_download_widget_constructed (GObject *object)
{
  EphyDownloadWidget *widget = EPHY_DOWNLOAD_WIDGET (object);
  WebKitDownload *download;
  const char *action_icon_name = NULL;
  GError *error = NULL;

  G_OBJECT_CLASS (ephy_download_widget_parent_class)->constructed (object);

  gtk_widget_set_margin_start (GTK_WIDGET (widget), 12);
  gtk_widget_set_margin_end (GTK_WIDGET (widget), 12);
  gtk_widget_set_margin_top (GTK_WIDGET (widget), 12);
  gtk_widget_set_margin_bottom (GTK_WIDGET (widget), 12);

  widget->icon = gtk_image_new ();
  gtk_widget_set_margin_end (widget->icon, 4);
  gtk_widget_set_halign (widget->icon, GTK_ALIGN_START);
  update_download_icon (widget);
  gtk_grid_attach (GTK_GRID (widget), widget->icon, 0, 0, 1, 1);
  gtk_widget_show (widget->icon);

  widget->filename = gtk_label_new (NULL);
  gtk_widget_set_hexpand (widget->filename, true);
  gtk_widget_set_valign (widget->filename, GTK_ALIGN_CENTER);
  gtk_label_set_xalign (GTK_LABEL (widget->filename), 0);
  gtk_label_set_max_width_chars (GTK_LABEL (widget->filename), 30);
  gtk_label_set_ellipsize (GTK_LABEL (widget->filename), PANGO_ELLIPSIZE_END);
  update_download_destination (widget);
  gtk_grid_attach (GTK_GRID (widget), widget->filename, 1, 0, 1, 1);
  gtk_widget_show (widget->filename);

  widget->progress = gtk_progress_bar_new ();
  gtk_widget_set_valign (widget->progress, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top (widget->progress, 6);
  gtk_widget_set_margin_bottom (widget->progress, 6);
  gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (widget->progress), 0.05);
  gtk_grid_attach (GTK_GRID (widget), widget->progress, 0, 1, 2, 1);
  if (ephy_download_is_active (widget->download))
    gtk_widget_show (widget->progress);

  widget->status = gtk_label_new (NULL);
  gtk_widget_set_valign (widget->status, GTK_ALIGN_CENTER);
  gtk_label_set_xalign (GTK_LABEL (widget->status), 0);
  g_object_set (widget->status, "width-request", 260, NULL);
  gtk_label_set_max_width_chars (GTK_LABEL (widget->status), 30);
  gtk_label_set_ellipsize (GTK_LABEL (widget->status), PANGO_ELLIPSIZE_END);
  if (ephy_download_failed (widget->download, &error)) {
    char *error_msg;

    error_msg = g_strdup_printf (_("Error downloading: %s"), error->message);
    update_status_label (widget, error_msg);
    g_free (error_msg);
  } else if (ephy_download_succeeded (widget->download)) {
    update_status_label (widget, _("Finished"));
  } else {
    update_status_label (widget, _("Starting…"));
  }
  gtk_grid_attach (GTK_GRID (widget), widget->status, 0, 2, 2, 1);
  gtk_widget_show (widget->status);

  if (ephy_download_succeeded (widget->download))
    action_icon_name = "folder-open-symbolic";
  else if (ephy_download_failed (widget->download, NULL))
    action_icon_name = "list-remove-symbolic";
  else
    action_icon_name = "window-close-symbolic";
  widget->action_button = gtk_button_new_from_icon_name (action_icon_name, GTK_ICON_SIZE_MENU);
  g_signal_connect_swapped (widget->action_button, "clicked",
                            G_CALLBACK (widget_action_button_clicked_cb),
                            widget);
  gtk_widget_set_valign (widget->action_button, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start (widget->action_button, 10);
  gtk_style_context_add_class (gtk_widget_get_style_context (widget->action_button),
                               "circular");
  gtk_grid_attach (GTK_GRID (widget), widget->action_button, 3, 0, 1, 3);
  gtk_widget_show (widget->action_button);

  download = ephy_download_get_webkit_download (widget->download);
  g_signal_connect (download, "notify::estimated-progress",
                    G_CALLBACK (download_progress_cb),
                    widget);
  g_signal_connect (download, "notify::destination",
                    G_CALLBACK (download_destination_changed_cb),
                    widget);
  g_signal_connect (widget->download, "completed",
                    G_CALLBACK (download_finished_cb),
                    widget);
  g_signal_connect (widget->download, "error",
                    G_CALLBACK (download_failed_cb),
                    widget);
  g_signal_connect (widget->download, "notify::content-type",
                    G_CALLBACK (download_content_type_changed_cb),
                    widget);
}

static void
ephy_download_widget_class_init (EphyDownloadWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ephy_download_widget_constructed;
  object_class->get_property = ephy_download_widget_get_property;
  object_class->set_property = ephy_download_widget_set_property;
  object_class->dispose = ephy_download_widget_dispose;

  /**
   * EphyDownloadWidget::download:
   *
   * The EphyDownload that this widget is showing.
   */

  obj_properties[PROP_DOWNLOAD] =
    g_param_spec_object ("download",
                         "An EphyDownload object",
                         "The EphyDownload shown by this widget",
                         G_TYPE_OBJECT,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, obj_properties);
/*CHB TODO tobedeleted
  g_object_class_install_property (object_class, PROP_DOWNLOAD,
                                   g_param_spec_object ("download",
                                                        "An EphyDownload object",
                                                        "The EphyDownload shown by this widget",
                                                        G_TYPE_OBJECT,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));
}

static void
smallify_label (GtkLabel *label)
{
        PangoAttrList *attrs;
        attrs = pango_attr_list_new ();
        pango_attr_list_insert (attrs, pango_attr_scale_new (PANGO_SCALE_SMALL));
        gtk_label_set_attributes (label, attrs);
        pango_attr_list_unref (attrs);
}

static void
create_widget (EphyDownloadWidget *widget)
{

  GtkWidget *grid;
  GtkWidget *icon;
  GtkWidget *text;
  GtkWidget *button;
  GtkWidget *menu_button;
  GtkWidget *remain;
  
  grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid), 6);

  button = gtk_button_new ();
  menu_button = gtk_menu_button_new ();
  gtk_menu_button_set_direction (GTK_MENU_BUTTON (menu_button), GTK_ARROW_UP);

  icon = gtk_image_new ();

  text = gtk_label_new (NULL);
  smallify_label (GTK_LABEL (text));
  gtk_misc_set_alignment (GTK_MISC (text), 0, 0.5);
  gtk_label_set_ellipsize (GTK_LABEL (text), PANGO_ELLIPSIZE_END);
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_LABEL (text)), "filename");

  remain = gtk_label_new (_("Starting…"));
  smallify_label (GTK_LABEL (remain));
  gtk_misc_set_alignment (GTK_MISC (remain), 0, 0.5);
  gtk_label_set_ellipsize (GTK_LABEL (remain), PANGO_ELLIPSIZE_END);

  gtk_grid_attach (GTK_GRID (grid), icon, 0, 0, 1, 2);
  gtk_grid_attach (GTK_GRID (grid), text, 1, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), remain, 1, 1, 1, 1);

  widget->priv->text = text;
  widget->priv->icon = icon;
  widget->priv->button = button;
  widget->priv->remaining = remain;
  widget->priv->menu_button = menu_button;
  widget->priv->finished = TRUE;//CHB assume success in any case
  widget->priv->timeout_id = 0;//CHB
  
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_HALF);

  gtk_container_add (GTK_CONTAINER (button), grid);

  gtk_box_pack_start (GTK_BOX (widget), button, FALSE, FALSE, 0);
  gtk_box_pack_end (GTK_BOX (widget), menu_button, FALSE, FALSE, 0);

  g_signal_connect (button, "clicked",
                    G_CALLBACK (download_clicked_cb), widget);
  g_signal_connect_swapped (menu_button, "clicked",
                            G_CALLBACK (widget_attention_unneeded), widget);

  gtk_widget_show_all (button);
  // gtk_widget_show_all (menu_button); CHB
*/
}

static void
ephy_download_widget_init (EphyDownloadWidget *widget)
{
}

/**
 * ephy_download_widget_get_download:
 * @widget: an #EphyDownloadWidget
 *
 * Gets the #EphyDownload that @widget is showing.
 *
 * Returns: (transfer none): an #EphyDownload.
 **/
EphyDownload *
ephy_download_widget_get_download (EphyDownloadWidget *widget)
{

  g_assert (EPHY_IS_DOWNLOAD_WIDGET (widget));
  return widget->download;
/*CHB TODO check tobedeleted
  g_return_val_if_fail (EPHY_IS_DOWNLOAD_WIDGET (widget), NULL);
  return widget->priv->download;
}

/**
 * ephy_download_widget_download_is_finished:
 * @widget: an #EphyDownloadWidget
 *
 * Whether the download finished
 *
 * Returns: %TRUE if download operation finished or %FALSE otherwise
 ** /
gboolean
ephy_download_widget_download_is_finished (EphyDownloadWidget *widget)
{
  nof_widgets_active = 0; //CHB
  g_return_val_if_fail (EPHY_IS_DOWNLOAD_WIDGET (widget), FALSE);
  return widget->priv->finished;
*/
}

/**
 * ephy_download_widget_new:
 * @ephy_download: the #EphyDownload that @widget is wrapping
 *
 * Creates an #EphyDownloadWidget to wrap @ephy_download. It also associates
 * @ephy_download to it.
 *
 * Returns: a new #EphyDownloadWidget
 **/
GtkWidget *
ephy_download_widget_new (EphyDownload *ephy_download)
{
  EphyDownloadWidget *widget;

  g_assert (EPHY_IS_DOWNLOAD (ephy_download));

  //CHB TODO check
  if(nof_widgets_active > MAX_NOF_WIDGETS_ACTIVE) return NULL;
  nof_widgets_active++;
  //eof CHB  

  widget = g_object_new (EPHY_TYPE_DOWNLOAD_WIDGET,
                         "download", ephy_download, NULL);

  return GTK_WIDGET (widget);
}
