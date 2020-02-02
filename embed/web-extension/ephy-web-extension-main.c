/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2012 Igalia S.L.
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

#include "ephy-web-extension.h"
#include "ephy-debug.h"
#include "ephy-file-helpers.h"
#include "ephy-settings.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"

static EphyWebExtension *extension = NULL;

/*CHB TODO toberemoved
static void
name_lost_cb (GDBusConnection *connection,
                         const gchar *name,
                         gpointer user_data)
{
  if (!connection) 
	  g_printerr("ephy-web-extension_main: name lost ... dbus connection is NULL\n"); 
  else f
	  g_printerr("ephy-web-extension_main: name lost ... dbus connection not NULL, %s\n", name);
}
*/

G_MODULE_EXPORT void
webkit_web_extension_initialize_with_user_data (WebKitWebExtension *webkit_extension,
                                                GVariant           *user_data)
{
  const char *server_address;
  const char *dot_dir;
  const char *adblock_data_dir;
  gboolean private_profile;
  gboolean browser_mode;
  GError *error = NULL;

  g_variant_get (user_data, "(m&s&s&sbb)", &server_address, &dot_dir, &adblock_data_dir, &private_profile, &browser_mode);

  if (!server_address) {
    g_warning ("UI process did not start D-Bus server, giving up.");
    return;
  }

  if (!ephy_file_helpers_init (dot_dir, 0, &error)) {
    g_warning ("Failed to initialize file helpers: %s", error->message);
    g_error_free (error);
  }

  ephy_debug_init ();

  extension = ephy_web_extension_get ();

  ephy_web_extension_initialize (extension,
                                 webkit_extension,
                                 server_address,
                                 adblock_data_dir,
                                 private_profile,
                                 browser_mode);
  /*CHB TODO toberemoved / checked
  web_extension = ephy_web_extension_get ();

  ephy_web_extension_initialize (web_extension, extension, dot_dir, private_profile);

  service_name = g_strdup_printf ("%s-%s", EPHY_WEB_EXTENSION_SERVICE_NAME, extension_id);

  g_bus_own_name (G_BUS_TYPE_SESSION,
                  service_name,
                  G_BUS_NAME_OWNER_FLAGS_NONE,
                  NULL,
                  (GBusNameAcquiredCallback)name_acquired_cb,
                  (GBusNameLostCallback)name_lost_cb, //CHB NULL,
                  web_extension, NULL);

  g_free (service_name);
  */
}

static void __attribute__((destructor))
ephy_web_extension_shutdown (void)
{
  if (extension)
    g_object_unref (extension);

  ephy_settings_shutdown ();
  ephy_file_helpers_shutdown ();
}

#pragma GCC diagnostic pop
