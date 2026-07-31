/* ui.h - GTK3 front-end */
#ifndef APPACHE_UI_H
#define APPACHE_UI_H

#include <gtk/gtk.h>

/* appache_activate: GtkApplication "activate" handler - builds the main window. */
void appache_activate(GtkApplication *app, gpointer user_data);

#endif /* APPACHE_UI_H */
