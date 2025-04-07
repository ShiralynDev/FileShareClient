#include <gtk/gtk.h>
#include <curl/curl.h>
#include <string.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *username_entry;
    GtkWidget *password_entry;
    GtkWidget *content_box;  // Main container to switch content
    GtkWidget *login_box;    // Initial login UI
    GtkWidget *files_box;    // Files list UI
} AppWidgets;

// Forward declaration
static void on_back_button_clicked(GtkWidget *widget, gpointer data);

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    char **response_ptr = (char **)userp;

    *response_ptr = g_strndup(contents, real_size);
    return real_size;
}

char* base64UrlSafeEncode(const unsigned char *input, size_t len) {
    static const char base64_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    size_t out_len = 4 * ((len + 2) / 3);
    char *encoded = malloc(out_len + 1);
    if (!encoded) return NULL;

    size_t i, j = 0;
    for (i = 0; i < len; i += 3) {
        unsigned int octet_a = input[i];
        unsigned int octet_b = (i + 1 < len) ? input[i + 1] : 0;
        unsigned int octet_c = (i + 2 < len) ? input[i + 2] : 0;

        unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        encoded[j++] = base64_table[(triple >> 18) & 0x3F];
        encoded[j++] = base64_table[(triple >> 12) & 0x3F];
        encoded[j++] = (i + 1 < len) ? base64_table[(triple >> 6) & 0x3F] : '=';
        encoded[j++] = (i + 2 < len) ? base64_table[triple & 0x3F] : '=';
    }

    // Null-terminate
    encoded[j] = '\0';

    // Strip any trailing '='
    while (j > 0 && encoded[j - 1] == '=') {
        encoded[--j] = '\0';
    }

    return encoded;
}

static void on_file_chooser_response(GtkDialog *file_chooser, int response_id, gpointer user_data) {
    struct {
        const char *filename;
        const char *username;
        const char *password;
    } *download_data = user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(file_chooser));
        char *save_path = g_file_get_path(file);

        if (!save_path) {
            g_printerr("Failed to get save path.\n");
            g_object_unref(file);
            gtk_window_destroy(GTK_WINDOW(file_chooser));
            return;
        }

        g_print("File will be saved to: %s\n", save_path);

        char *url = g_strdup_printf("http://localhost:8080/downloadFile/%s", download_data->filename);
        FILE *file_handle = fopen(save_path, "wb");
        if (!file_handle) {
            g_printerr("Failed to open file for writing: %s\n", save_path);
            g_free(save_path);
            g_free(url);
            g_object_unref(file);
            gtk_window_destroy(GTK_WINDOW(file_chooser));
            return;
        }

        curl_global_init(CURL_GLOBAL_DEFAULT);
        CURL *curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL); // Write directly to file
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, file_handle);

            // Generate the Authorization header
            char *usernameB64 = base64UrlSafeEncode((unsigned char *)download_data->username, strlen(download_data->username));
            char *passwordB64 = base64UrlSafeEncode((unsigned char *)download_data->password, strlen(download_data->password));
            char header[256] = "Authorization: ";
            strcat(header, usernameB64);
            strcat(header, ".");
            strcat(header, passwordB64);
            struct curl_slist *list = curl_slist_append(NULL, header);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

            curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                g_printerr("Download failed: %s\n", curl_easy_strerror(res));
            } else {
                g_print("File downloaded successfully: %s\n", save_path);
            }

            curl_slist_free_all(list);
            curl_easy_cleanup(curl);
            free(usernameB64);
            free(passwordB64);
        }

        fclose(file_handle);
        g_free(save_path);
        g_free(url);
        g_object_unref(file);
        curl_global_cleanup();
    } else {
        g_print("File save canceled.\n");
    }

    g_free(download_data->filename);
    g_free(download_data->username);
    g_free(download_data->password);
    g_free(download_data);

    gtk_window_destroy(GTK_WINDOW(file_chooser));
}

static void on_download_response(GtkDialog *dialog, int response_id, gpointer user_data) {
    struct {
        const char *filename;
        const char *username;
        const char *password;
    } *download_data = user_data;

    if (response_id == GTK_RESPONSE_YES) {
        g_print("Download confirmed for: %s\n", download_data->filename);

        // Create a file chooser dialog for saving the file
        GtkWidget *file_chooser = gtk_file_chooser_dialog_new(
            "Save File",
            GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(dialog))),
            GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Save", GTK_RESPONSE_ACCEPT,
            NULL
        );

        // Set a default filename
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(file_chooser), download_data->filename);

        // Connect to the response signal
        g_signal_connect(file_chooser, "response", G_CALLBACK(on_file_chooser_response), download_data);

        // Show the file chooser dialog
        gtk_widget_show(file_chooser);
    } else {
        g_print("Download canceled for: %s\n", download_data->filename);
        g_free(download_data->filename);
        g_free(download_data->username);
        g_free(download_data->password);
        g_free(download_data);
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_download_button_clicked(GtkWidget *widget, gpointer data) {
    const char *filename = (const char *)data; // Filename is passed as user data
    AppWidgets *widgets = (AppWidgets *)g_object_get_data(G_OBJECT(widget), "app_widgets");

    const char *username = gtk_editable_get_text(GTK_EDITABLE(widgets->username_entry));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(widgets->password_entry));

    // Extract file extension
    const char *file_extension = strrchr(filename, '.');
    if (!file_extension) {
        file_extension = "unknown"; // Default if no extension is found
    } else {
        file_extension++; // Skip the dot
    }

    // Create a confirmation dialog
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(gtk_widget_get_root(widget)), // Use gtk_widget_get_root() in GTK4
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Do you want to download the file '%s'?\nFile type: %s",
        filename,
        file_extension
    );

    // Create a structure to pass multiple parameters
    struct {
        const char *filename;
        const char *username;
        const char *password;
    } *download_data = g_malloc(sizeof(*download_data));
    download_data->filename = g_strdup(filename);
    download_data->username = g_strdup(username);
    download_data->password = g_strdup(password);

    // Connect to the response signal
    g_signal_connect(dialog, "response", G_CALLBACK(on_download_response), download_data);

    // Show the dialog
    gtk_widget_show(dialog);
}

void cleanup_filename_copy(gpointer data, GClosure *closure) {
    g_free(data);  // Use g_free for cleanup
}

static void clear_container(GtkWidget *container) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(container)) != NULL) {
        gtk_widget_unparent(child);
    }
}

// Modern GTK4 approach to showing a simple alert dialog
static void show_alert_dialog(GtkWindow *parent, const char *title, const char *message, GtkMessageType type) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new(message);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_set_detail(dialog, title);
    
    if (type == GTK_MESSAGE_ERROR) {
        gtk_alert_dialog_set_buttons(dialog, (const char *[]){"OK", NULL});
    } else {
        gtk_alert_dialog_set_buttons(dialog, (const char *[]){"OK", NULL});
    }
    
    gtk_alert_dialog_show(dialog, parent);
    g_object_unref(dialog);
}

static void show_files_ui(AppWidgets *widgets, char *response) {
    // Clear the login UI
    gtk_widget_set_visible(widgets->login_box, FALSE);
    
    // Create files UI if it doesn't exist
    if (!widgets->files_box) {
        widgets->files_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_widget_set_margin_start(widgets->files_box, 10);
        gtk_widget_set_margin_end(widgets->files_box, 10);
        gtk_widget_set_margin_top(widgets->files_box, 10);
        gtk_widget_set_margin_bottom(widgets->files_box, 10);
        gtk_box_append(GTK_BOX(widgets->content_box), widgets->files_box);
    } else {
        clear_container(widgets->files_box);
    }
    
    // Add a header
    GtkWidget *header = gtk_label_new("Available Files:");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(widgets->files_box), header);
    
    // Split response by newlines and create an entry for each line
    char *response_copy = g_strdup(response);
    char *line = strtok(response_copy, "\n");
    while (line != NULL) {
        // Skip empty lines
        if (strlen(line) > 0) {
            // Create a horizontal box for each file entry
            GtkWidget *file_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            
            // Create text entry for the filename
            GtkWidget *entry = gtk_entry_new();
            gtk_editable_set_text(GTK_EDITABLE(entry), line);
            gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
            gtk_widget_set_hexpand(entry, TRUE);
            
            // Create download button
            GtkWidget *download_button = gtk_button_new_with_label("Download");
            char *filename_copy = g_strdup(line); // Make a copy of the filename
            g_signal_connect(download_button, "clicked", G_CALLBACK(on_download_button_clicked), filename_copy);

            // Store the AppWidgets pointer in the button for later use
            g_object_set_data(G_OBJECT(download_button), "app_widgets", widgets);
            
            // Add widgets to the row
            gtk_box_append(GTK_BOX(file_row), entry);
            gtk_box_append(GTK_BOX(file_row), download_button);
            
            // Add row to the files box
            gtk_box_append(GTK_BOX(widgets->files_box), file_row);
        }
        
        line = strtok(NULL, "\n");
    }
    g_free(response_copy);
    
    // Add a "Back" button to return to login UI
    GtkWidget *back_button = gtk_button_new_with_label("Back to Login");
    g_signal_connect(back_button, "clicked", G_CALLBACK(on_back_button_clicked), widgets);
    gtk_box_append(GTK_BOX(widgets->files_box), back_button);
    
    // Show the files UI
    gtk_widget_set_visible(widgets->files_box, TRUE);
}

static void on_back_button_clicked(GtkWidget *widget, gpointer data) {
    AppWidgets *widgets = (AppWidgets *)data;
    
    // Hide files UI and show login UI
    gtk_widget_set_visible(widgets->files_box, FALSE);
    gtk_widget_set_visible(widgets->login_box, TRUE);
}

static bool hasAuth(GtkWidget *widget, gpointer data, char *username, char *password) {
    AppWidgets *widgets = (AppWidgets *)data;
    CURL *curl;
    CURLcode res;
    char *url = "http://localhost:8080/getFiles/";
    char *response = NULL;
    bool success = FALSE;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    struct curl_slist *list = NULL;
  
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
        char* usernameB64 = base64UrlSafeEncode((unsigned char *)username, strlen(username));
        char* passwordB64 = base64UrlSafeEncode((unsigned char *)password, strlen(password));
        char header[256] = "Authorization: ";
        strcat(header, usernameB64);
        strcat(header, ".");
        strcat(header, passwordB64);
        list = curl_slist_append(list, header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
        res = curl_easy_perform(curl);

        free(usernameB64);
        free(passwordB64);
  
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            char error_msg[256];
            snprintf(error_msg, 256, "Connection error: %s", curl_easy_strerror(res));
            show_alert_dialog(GTK_WINDOW(widgets->window), "Connection Error", error_msg, GTK_MESSAGE_ERROR);
        } else {
            g_print("Response: %s\n", response);
            if (response && strlen(response) > 0) {
                // Show the files UI with the response
                show_files_ui(widgets, response);
                success = TRUE;
            } else {
                show_alert_dialog(GTK_WINDOW(widgets->window), "Authentication Failed", 
                                 "No files found or authentication failed.", GTK_MESSAGE_ERROR);
            }
            g_free(response);
        }
        
        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
    }
    
    curl_global_cleanup();
    return success;
}

static void on_connect_button_clicked(GtkWidget *widget, gpointer data) {
    g_print("Connect button clicked\n");

    AppWidgets *widgets = (AppWidgets *)data;

    const char *username = gtk_editable_get_text(GTK_EDITABLE(widgets->username_entry));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(widgets->password_entry));

    hasAuth(widget, widgets, (char *)username, (char *)password);
}

static void activate(GtkApplication *app, gpointer user_data) {
    GtkBuilder *builder = gtk_builder_new();
    GError *error = NULL;

    if (!gtk_builder_add_from_file(builder, "builder.ui", &error)) {
        g_printerr("Error loading UI file: %s\n", error->message);
        g_clear_error(&error);
        return;
    }

    GObject *window_obj = gtk_builder_get_object(builder, "window");
    GtkWindow *window = GTK_WINDOW(window_obj);
    gtk_window_set_application(window, app);

    AppWidgets *widgets = g_malloc(sizeof(AppWidgets));
    widgets->window = GTK_WIDGET(window);
    widgets->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    widgets->files_box = NULL;
    
    // Get the main container from the builder file
    GtkWidget *main_container = GTK_WIDGET(gtk_builder_get_object(builder, "main_box"));
    if (!main_container) {
        // If there's no main_box in the UI file, use the window's content area
        GtkWidget *window_content = gtk_window_get_child(window);
        if (window_content) {
            gtk_widget_unparent(window_content);
        }
        gtk_window_set_child(window, widgets->content_box);
    } else {
        // Replace the main container with our content box
        GtkWidget *parent = gtk_widget_get_parent(main_container);
        gtk_widget_unparent(main_container);
        gtk_box_append(GTK_BOX(parent), widgets->content_box);
    }
    
    // Create a box for the login UI
    widgets->login_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(widgets->login_box, 10);
    gtk_widget_set_margin_end(widgets->login_box, 10);
    gtk_widget_set_margin_top(widgets->login_box, 10);
    gtk_widget_set_margin_bottom(widgets->login_box, 10);
    gtk_box_append(GTK_BOX(widgets->content_box), widgets->login_box);
    
    // Create username label and entry
    GtkWidget *username_label = gtk_label_new("Username:");
    gtk_widget_set_halign(username_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(widgets->login_box), username_label);
    
    widgets->username_entry = gtk_entry_new();
    gtk_widget_set_hexpand(widgets->username_entry, TRUE);
    gtk_box_append(GTK_BOX(widgets->login_box), widgets->username_entry);
    
    // Create password label and entry
    GtkWidget *password_label = gtk_label_new("Password:");
    gtk_widget_set_halign(password_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(widgets->login_box), password_label);
    
    widgets->password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(widgets->password_entry), FALSE);
    gtk_widget_set_hexpand(widgets->password_entry, TRUE);
    gtk_box_append(GTK_BOX(widgets->login_box), widgets->password_entry);
    
    // Create connect button
    GtkWidget *connect_button = gtk_button_new_with_label("Connect");
    g_signal_connect(connect_button, "clicked", G_CALLBACK(on_connect_button_clicked), widgets);
    gtk_box_append(GTK_BOX(widgets->login_box), connect_button);

    gtk_widget_set_visible(GTK_WIDGET(window), TRUE);
    g_object_unref(builder);
}

int main(int argc, char *argv[]) {
#ifdef GTK_SRCDIR
    g_chdir(GTK_SRCDIR);
#endif

    GtkApplication *app = gtk_application_new("org.gtk.example", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}