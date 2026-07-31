/* ui.c - see ui.h
 *
 * A deliberately small GTK3 front-end: one main window, one list, a
 * handful of dialogs. All network/extraction work happens on a worker
 * thread; results are marshaled back to the GTK main loop via
 * g_idle_add() so GTK calls only ever happen on the main thread.
 */
#include <string.h>
#include <gtk/gtk.h>

#include "ui.h"
#include "appache.h"
#include "store.h"
#include "integrate.h"
#include "update.h"
#include "util.h"

typedef struct {
    GtkApplication *gtk_app;
    GtkWidget *window;
    GtkWidget *listbox;
    GtkWidget *stack;        /* "empty" vs "list" */
    GtkWidget *status_label;
    GtkWidget *add_button;
    GtkWidget *refresh_button;
    AppList *list;
    gboolean busy;
} appacheApp;

typedef enum {
    JOB_ADD_FILE,
    JOB_ADD_URL,
    JOB_CHECK_ALL,
    JOB_CHECK_ONE,
    JOB_APPLY_UPDATE
} JobType;

typedef struct {
    appacheApp *app;
    JobType type;
    char *input;     /* file path or URL, owned */
    char *entry_id;  /* owned, may be NULL */
    AppEntry *new_entry; /* set by worker for ADD jobs */
    char *error;     /* set by worker on failure, owned */
} Job;

static void refresh_list_ui(appacheApp *app);
static void set_busy(appacheApp *app, gboolean busy, const char *status);
static void start_job(appacheApp *app, JobType type, const char *input, const char *entry_id);
static gboolean job_done_idle(gpointer data);

/* ---------- status / busy state ---------------------------------------- */

static void set_busy(appacheApp *app, gboolean busy, const char *status) {
    app->busy = busy;
    gtk_widget_set_sensitive(app->add_button, !busy);
    gtk_widget_set_sensitive(app->refresh_button, !busy);
    gtk_widget_set_sensitive(app->listbox, !busy);
    if (status)
        gtk_label_set_text(GTK_LABEL(app->status_label), status);
}

static void show_error(appacheApp *app, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK, "%s", message ? message : "Something went wrong.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* ---------- background job plumbing ------------------------------------ */

static gpointer worker_thread(gpointer data) {
    Job *job = data;

    switch (job->type) {
    case JOB_ADD_FILE:
        job->new_entry = integrate_appimage(job->input, &job->error);
        break;
    case JOB_ADD_URL:
        job->new_entry = add_from_url(job->input, &job->error);
        break;
    case JOB_CHECK_ALL: {
        AppEntry *e;
        for (e = job->app->list->head; e; e = e->next) check_update(e);
        break;
    }
    case JOB_CHECK_ONE: {
        AppEntry *e = list_find(job->app->list, job->entry_id);
        if (e) check_update(e);
        break;
    }
    case JOB_APPLY_UPDATE: {
        AppEntry *e = list_find(job->app->list, job->entry_id);
        if (e) apply_update(e, &job->error);
        break;
    }
    }

    g_idle_add(job_done_idle, job);
    return NULL;
}

static gboolean job_done_idle(gpointer data) {
    Job *job = data;
    appacheApp *app = job->app;

    switch (job->type) {
    case JOB_ADD_FILE:
    case JOB_ADD_URL:
        if (job->new_entry) {
            list_append(app->list, job->new_entry);
            store_save(app->list);
            {
                char *msg = xasprintf("Added %s", job->new_entry->name);
                set_busy(app, FALSE, msg);
                free(msg);
            }
        } else {
            set_busy(app, FALSE, "Add failed");
            show_error(app, job->error ? job->error : "Could not add that AppImage.");
        }
        break;

    case JOB_CHECK_ALL:
    case JOB_CHECK_ONE:
        store_save(app->list);
        set_busy(app, FALSE, "Update check complete");
        break;

    case JOB_APPLY_UPDATE:
        store_save(app->list);
        if (job->error) {
            set_busy(app, FALSE, "Update failed");
            show_error(app, job->error);
        } else {
            set_busy(app, FALSE, "Updated");
        }
        break;
    }

    refresh_list_ui(app);

    free(job->input);
    free(job->entry_id);
    free(job->error);
    free(job);
    return G_SOURCE_REMOVE;
}

static void start_job(appacheApp *app, JobType type, const char *input, const char *entry_id) {
    Job *job;
    GThread *thread;
    const char *status;

    if (app->busy) return;

    job = calloc(1, sizeof(*job));
    job->app = app;
    job->type = type;
    job->input = input ? xstrdup(input) : NULL;
    job->entry_id = entry_id ? xstrdup(entry_id) : NULL;

    switch (type) {
        case JOB_ADD_FILE:     status = "Integrating AppImage\xe2\x80\xa6"; break;
        case JOB_ADD_URL:      status = "Downloading\xe2\x80\xa6"; break;
        case JOB_CHECK_ALL:    status = "Checking for updates\xe2\x80\xa6"; break;
        case JOB_CHECK_ONE:    status = "Checking for updates\xe2\x80\xa6"; break;
        case JOB_APPLY_UPDATE: status = "Updating\xe2\x80\xa6"; break;
        default:                status = "Working\xe2\x80\xa6"; break;
    }
    set_busy(app, TRUE, status);

    thread = g_thread_new("appache-worker", worker_thread, job);
    g_thread_unref(thread);
}

/* ---------- list rendering ---------------------------------------------- */

static void on_launch_clicked(GtkButton *btn, gpointer user_data) {
    char *path = user_data;
    char *argv[] = { path, NULL };
    GError *err = NULL;
    if (!g_spawn_async(NULL, argv, NULL,
                        G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                        NULL, NULL, NULL, &err)) {
        g_warning("launch failed: %s", err ? err->message : "unknown error");
        if (err) g_error_free(err);
    }
    (void)btn;
}

static void on_update_clicked(GtkButton *btn, gpointer user_data) {
    appacheApp *app = g_object_get_data(G_OBJECT(btn), "appache-app");
    char *id = user_data;
    start_job(app, JOB_APPLY_UPDATE, NULL, id);
}

static void on_remove_response(GtkDialog *dialog, gint response, gpointer user_data) {
    if (response == GTK_RESPONSE_YES) {
        appacheApp *app = g_object_get_data(G_OBJECT(dialog), "appache-app");
        char *id = user_data;
        AppEntry *e = list_find(app->list, id);
        if (e) {
            remove_appimage(e);
            list_remove(app->list, id);
            store_save(app->list);
            refresh_list_ui(app);
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
    free(user_data);
}

static void on_remove_clicked(GtkButton *btn, gpointer user_data) {
    appacheApp *app = g_object_get_data(G_OBJECT(btn), "appache-app");
    char *id = xstrdup((char *)user_data);
    AppEntry *e = list_find(app->list, (char *)user_data);
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO, "Remove \xe2\x80\x9c%s\xe2\x80\x9d?",
        e ? e->name : "this app");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
        "The AppImage, its menu entry and its icon will be moved to the trash.");
    g_object_set_data(G_OBJECT(dialog), "appache-app", app);
    g_signal_connect(dialog, "response", G_CALLBACK(on_remove_response), id);
    gtk_widget_show(dialog);
}

static void on_source_clicked(GtkButton *btn, gpointer user_data) {
    appacheApp *app = g_object_get_data(G_OBJECT(btn), "appache-app");
    char *id = user_data;
    AppEntry *e = list_find(app->list, id);
    GtkWidget *dialog, *content, *grid;
    GtkWidget *radio_github, *radio_direct;
    GtkWidget *repo_entry, *url_entry;
    if (!e) return;

    dialog = gtk_dialog_new_with_buttons("Update source", GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_OK, NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_add(GTK_CONTAINER(content), grid);

    radio_github = gtk_radio_button_new_with_label(NULL, "GitHub repo (owner/repo)");
    radio_direct = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(radio_github), "Direct download URL");
    gtk_grid_attach(GTK_GRID(grid), radio_github, 0, 0, 2, 1);

    repo_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(repo_entry), "e.g. someuser/someproject");
    if (e->source_repo && *e->source_repo)
        gtk_entry_set_text(GTK_ENTRY(repo_entry), e->source_repo);
    gtk_grid_attach(GTK_GRID(grid), repo_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Repo:"), 0, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), radio_direct, 0, 2, 2, 1);
    url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(url_entry), "https://\xe2\x80\xa6/App.AppImage");
    if (e->source_url && *e->source_url)
        gtk_entry_set_text(GTK_ENTRY(url_entry), e->source_url);
    gtk_grid_attach(GTK_GRID(grid), url_entry, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("URL:"), 0, 3, 1, 1);

    if (e->source_type == SRC_DIRECT)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_direct), TRUE);
    else
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_github), TRUE);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        gboolean use_github = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_github));
        free(e->source_repo); e->source_repo = NULL;
        free(e->source_url);  e->source_url = NULL;

        if (use_github) {
            const char *txt = gtk_entry_get_text(GTK_ENTRY(repo_entry));
            e->source_type = (*txt) ? SRC_GITHUB : SRC_NONE;
            e->source_repo = xstrdup(txt);
            e->source_url = xstrdup("");
        } else {
            const char *txt = gtk_entry_get_text(GTK_ENTRY(url_entry));
            e->source_type = (*txt) ? SRC_DIRECT : SRC_NONE;
            e->source_url = xstrdup(txt);
            e->source_repo = xstrdup("");
        }
        store_save(app->list);
        gtk_widget_destroy(dialog);
        if (e->source_type != SRC_NONE)
            start_job(app, JOB_CHECK_ONE, NULL, e->id);
        else
            refresh_list_ui(app);
    } else {
        gtk_widget_destroy(dialog);
    }
    (void)btn;
}

static void free_closure_str(gpointer data, GClosure *closure) {
    (void)closure;
    free(data);
}

static GtkWidget *make_row(appacheApp *app, AppEntry *e) {    GtkWidget *row, *box, *icon, *text_box, *name_label, *ver_label;
    GtkWidget *btn_box, *launch_btn, *update_btn, *source_btn, *remove_btn;
    GdkPixbuf *pixbuf = NULL;

    row = gtk_list_box_row_new();
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    gtk_container_add(GTK_CONTAINER(row), box);

    if (e->icon_path && *e->icon_path)
        pixbuf = gdk_pixbuf_new_from_file_at_scale(e->icon_path, 48, 48, TRUE, NULL);
    if (pixbuf) {
        icon = gtk_image_new_from_pixbuf(pixbuf);
        g_object_unref(pixbuf);
    } else {
        icon = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_DIALOG);
    }
    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);

    text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box), text_box, TRUE, TRUE, 0);

    name_label = gtk_label_new(NULL);
    {
        char *markup = g_markup_printf_escaped("<b>%s</b>", e->name);
        gtk_label_set_markup(GTK_LABEL(name_label), markup);
        g_free(markup);
    }
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);
    gtk_box_pack_start(GTK_BOX(text_box), name_label, FALSE, FALSE, 0);

    {
        char *status;
        const char *src_desc = "no update source";
        if (e->source_type == SRC_GITHUB) src_desc = "GitHub";
        else if (e->source_type == SRC_DIRECT) src_desc = "direct link";

        if (e->update_available)
            status = xasprintf("v%s installed \xc2\xb7 %s \xc2\xb7 update available",
                                (e->version && *e->version) ? e->version : "?", src_desc);
        else
            status = xasprintf("v%s installed \xc2\xb7 %s",
                                (e->version && *e->version) ? e->version : "?", src_desc);
        ver_label = gtk_label_new(status);
        free(status);
    }
    gtk_label_set_xalign(GTK_LABEL(ver_label), 0.0);
    gtk_style_context_add_class(gtk_widget_get_style_context(ver_label), "dim-label");
    gtk_box_pack_start(GTK_BOX(text_box), ver_label, FALSE, FALSE, 0);

    btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_valign(btn_box, GTK_ALIGN_CENTER);
    gtk_box_pack_end(GTK_BOX(box), btn_box, FALSE, FALSE, 0);

    launch_btn = gtk_button_new_with_label("Launch");
    g_signal_connect_data(launch_btn, "clicked", G_CALLBACK(on_launch_clicked),
                           xstrdup(e->appimage_path), free_closure_str, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), launch_btn, FALSE, FALSE, 0);

    if (e->update_available) {
        update_btn = gtk_button_new_with_label("Update");
        gtk_style_context_add_class(gtk_widget_get_style_context(update_btn), "suggested-action");
        g_object_set_data(G_OBJECT(update_btn), "appache-app", app);
        g_signal_connect_data(update_btn, "clicked", G_CALLBACK(on_update_clicked),
                               xstrdup(e->id), free_closure_str, 0);
        gtk_box_pack_start(GTK_BOX(btn_box), update_btn, FALSE, FALSE, 0);
    }

    source_btn = gtk_button_new_with_label("Source\xe2\x80\xa6");
    g_object_set_data(G_OBJECT(source_btn), "appache-app", app);
    g_signal_connect_data(source_btn, "clicked", G_CALLBACK(on_source_clicked),
                           xstrdup(e->id), free_closure_str, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), source_btn, FALSE, FALSE, 0);

    remove_btn = gtk_button_new_from_icon_name("user-trash-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(remove_btn, "Remove");
    g_object_set_data(G_OBJECT(remove_btn), "appache-app", app);
    g_signal_connect_data(remove_btn, "clicked", G_CALLBACK(on_remove_clicked),
                           xstrdup(e->id), free_closure_str, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), remove_btn, FALSE, FALSE, 0);

    gtk_widget_show_all(row);
    return row;
}

static void refresh_list_ui(appacheApp *app) {
    GList *children, *iter;
    AppEntry *e;

    children = gtk_container_get_children(GTK_CONTAINER(app->listbox));
    for (iter = children; iter; iter = iter->next)
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    for (e = app->list->head; e; e = e->next)
        gtk_container_add(GTK_CONTAINER(app->listbox), make_row(app, e));

    gtk_stack_set_visible_child_name(GTK_STACK(app->stack),
        app->list->count > 0 ? "list" : "empty");
}

/* ---------- add flows ---------------------------------------------------- */

static void add_file_path(appacheApp *app, const char *path) {
    start_job(app, JOB_ADD_FILE, path, NULL);
}

static void on_add_file_clicked(GtkButton *btn, gpointer user_data) {
    appacheApp *app = user_data;
    GtkFileChooserNative *chooser = gtk_file_chooser_native_new(
        "Select an AppImage", GTK_WINDOW(app->window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Add", "_Cancel");
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "AppImage files");
    gtk_file_filter_add_pattern(filter, "*.AppImage");
    gtk_file_filter_add_pattern(filter, "*.appimage");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        add_file_path(app, path);
        g_free(path);
    }
    g_object_unref(chooser);
    (void)btn;
}

static void on_add_url_clicked(GtkButton *btn, gpointer user_data) {
    appacheApp *app = user_data;
    GtkWidget *dialog, *content, *entry, *label;

    dialog = gtk_dialog_new_with_buttons("Add from URL", GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_OK, NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 6);

    label = gtk_label_new("Direct link to an .AppImage file:");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_container_add(GTK_CONTAINER(content), label);

    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "https://\xe2\x80\xa6/App.AppImage");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *url = gtk_entry_get_text(GTK_ENTRY(entry));
        if (url && *url) start_job(app, JOB_ADD_URL, url, NULL);
    }
    gtk_widget_destroy(dialog);
    (void)btn;
}

static void on_refresh_clicked(GtkButton *btn, gpointer user_data) {
    appacheApp *app = user_data;
    start_job(app, JOB_CHECK_ALL, NULL, NULL);
    (void)btn;
}

/* ---------- drag and drop ------------------------------------------------- */

static void on_drag_data_received(GtkWidget *widget, GdkDragContext *context,
                                   gint x, gint y, GtkSelectionData *data,
                                   guint info, guint time, gpointer user_data) {
    appacheApp *app = user_data;
    gchar **uris = gtk_selection_data_get_uris(data);
    int i;
    (void)widget; (void)x; (void)y; (void)info;

    if (uris) {
        for (i = 0; uris[i]; i++) {
            char *path = g_filename_from_uri(uris[i], NULL, NULL);
            if (path && (str_ends_with_ci(path, ".appimage"))) {
                add_file_path(app, path);
            }
            g_free(path);
        }
        g_strfreev(uris);
    }
    gtk_drag_finish(context, TRUE, FALSE, time);
}

/* ---------- window construction -------------------------------------------- */

void appache_activate(GtkApplication *gtk_app, gpointer user_data) {
    appacheApp *app;
    GtkWidget *header, *popover, *popover_box;
    GtkWidget *popover_add_file, *popover_add_url;
    GtkWidget *scrolled, *empty_label, *main_box;
    static const GtkTargetEntry targets[] = { { "text/uri-list", 0, 0 } };
    (void)user_data;

    app = calloc(1, sizeof(*app));
    app->gtk_app = gtk_app;
    app->list = store_load();

    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), "appache");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 560, 420);
    g_object_set_data(G_OBJECT(app->window), "appache-app-ptr", app);

    header = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "appache");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "AppImage manager");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(app->window), header);

    app->add_button = gtk_menu_button_new();
    gtk_button_set_image(GTK_BUTTON(app->add_button),
        gtk_image_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text(app->add_button, "Add an AppImage");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), app->add_button);

    popover = gtk_popover_new(app->add_button);
    popover_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(popover_box), 4);

    popover_add_file = gtk_model_button_new();
    g_object_set(popover_add_file, "text", "Add AppImage file\xe2\x80\xa6", NULL);
    g_signal_connect(popover_add_file, "clicked", G_CALLBACK(on_add_file_clicked), app);
    g_signal_connect_swapped(popover_add_file, "clicked", G_CALLBACK(gtk_widget_hide), popover);
    gtk_container_add(GTK_CONTAINER(popover_box), popover_add_file);

    popover_add_url = gtk_model_button_new();
    g_object_set(popover_add_url, "text", "Add from URL\xe2\x80\xa6", NULL);
    g_signal_connect(popover_add_url, "clicked", G_CALLBACK(on_add_url_clicked), app);
    g_signal_connect_swapped(popover_add_url, "clicked", G_CALLBACK(gtk_widget_hide), popover);
    gtk_container_add(GTK_CONTAINER(popover_box), popover_add_url);

    gtk_widget_show_all(popover_box);
    gtk_container_add(GTK_CONTAINER(popover), popover_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(app->add_button), popover);

    app->refresh_button = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(app->refresh_button, "Check for updates");
    g_signal_connect(app->refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), app);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), app->refresh_button);

    main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), main_box);

    app->stack = gtk_stack_new();
    gtk_box_pack_start(GTK_BOX(main_box), app->stack, TRUE, TRUE, 0);

    empty_label = gtk_label_new("No AppImages yet.\nDrag one in, or use the + button above.");
    gtk_label_set_justify(GTK_LABEL(empty_label), GTK_JUSTIFY_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(empty_label), "dim-label");
    gtk_widget_set_valign(empty_label, GTK_ALIGN_CENTER);
    gtk_stack_add_named(GTK_STACK(app->stack), empty_label, "empty");

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    app->listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->listbox), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), app->listbox);
    gtk_stack_add_named(GTK_STACK(app->stack), scrolled, "list");

    app->status_label = gtk_label_new("");
    gtk_widget_set_halign(app->status_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(app->status_label, 10);
    gtk_widget_set_margin_end(app->status_label, 10);
    gtk_widget_set_margin_top(app->status_label, 4);
    gtk_widget_set_margin_bottom(app->status_label, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(app->status_label), "dim-label");
    gtk_box_pack_end(GTK_BOX(main_box), app->status_label, FALSE, FALSE, 0);

    gtk_drag_dest_set(app->window, GTK_DEST_DEFAULT_ALL, targets, 1, GDK_ACTION_COPY);
    g_signal_connect(app->window, "drag-data-received", G_CALLBACK(on_drag_data_received), app);

    refresh_list_ui(app);
    gtk_widget_show_all(app->window);

    if (app->list->count > 0)
        start_job(app, JOB_CHECK_ALL, NULL, NULL);
}
