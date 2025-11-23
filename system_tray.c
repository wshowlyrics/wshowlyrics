#include "system_tray.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libappindicator/app-indicator.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <curl/curl.h>

// State
static AppIndicator *indicator = NULL;
static GtkWidget *menu = NULL;
static char *last_art_url = NULL;

// Fixed paths
static const char *ICON_DIR = "/tmp/lyrics-icons";
static const char *ICON_PATH = "/tmp/lyrics-icons/album-art.png";
static const char *ICON_NAME = "album-art";

// Memory buffer for CURL downloads
struct memory_buffer {
	char *data;
	size_t size;
};

static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct memory_buffer *mem = (struct memory_buffer *)userp;

	char *ptr = realloc(mem->data, mem->size + realsize + 1);
	if (!ptr) {
		fprintf(stderr, "Out of memory\n");
		return 0;
	}

	mem->data = ptr;
	memcpy(&(mem->data[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->data[mem->size] = 0;

	return realsize;
}

// Download image from URL to memory
static bool download_image(const char *url, struct memory_buffer *buffer) {
	CURL *curl = curl_easy_init();
	if (!curl) {
		return false;
	}

	buffer->data = malloc(1);
	buffer->size = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)buffer);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		free(buffer->data);
		buffer->data = NULL;
		return false;
	}

	return true;
}

// Check if image is square
static bool is_square_image(GdkPixbuf *pixbuf) {
	if (!pixbuf) {
		return false;
	}

	int width = gdk_pixbuf_get_width(pixbuf);
	int height = gdk_pixbuf_get_height(pixbuf);

	return width == height && width > 0;
}

// Save default icon from theme to file
static bool save_default_icon(const char *output_path) {
	GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
	GError *error = NULL;

	// Try to load audio-player icon from theme
	GdkPixbuf *icon = gtk_icon_theme_load_icon(icon_theme, "audio-player", 48, 0, &error);

	if (!icon) {
		// Fallback to audio-headphones or multimedia-player
		g_clear_error(&error);
		icon = gtk_icon_theme_load_icon(icon_theme, "audio-headphones", 48, 0, &error);
	}

	if (!icon) {
		// Final fallback to multimedia-player
		g_clear_error(&error);
		icon = gtk_icon_theme_load_icon(icon_theme, "multimedia-player", 48, 0, &error);
	}

	if (!icon) {
		fprintf(stderr, "Failed to load default icon from theme: %s\n",
			error ? error->message : "unknown error");
		if (error) g_error_free(error);
		return false;
	}

	// Save to file
	g_clear_error(&error);
	if (!gdk_pixbuf_save(icon, output_path, "png", &error, NULL)) {
		fprintf(stderr, "Failed to save default icon: %s\n", error->message);
		g_error_free(error);
		g_object_unref(icon);
		return false;
	}

	g_object_unref(icon);
	printf("Default icon saved to: %s\n", output_path);
	return true;
}

// Load image from URL (http:// or file://)
static GdkPixbuf* load_image_from_url(const char *url) {
	GError *error = NULL;
	GdkPixbuf *pixbuf = NULL;

	// Handle file:// URLs
	if (strncmp(url, "file://", 7) == 0) {
		const char *file_path = url + 7;
		pixbuf = gdk_pixbuf_new_from_file(file_path, &error);

		if (error) {
			fprintf(stderr, "Failed to load image from file: %s\n", error->message);
			g_error_free(error);
			return NULL;
		}
	}
	// Handle http:// or https:// URLs
	else if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
		struct memory_buffer buffer = {0};

		if (!download_image(url, &buffer)) {
			fprintf(stderr, "Failed to download image from URL\n");
			return NULL;
		}

		GdkPixbufLoader *loader = gdk_pixbuf_loader_new();

		if (!gdk_pixbuf_loader_write(loader, (const guchar *)buffer.data, buffer.size, &error)) {
			fprintf(stderr, "Failed to load image data: %s\n", error->message);
			g_error_free(error);
			g_object_unref(loader);
			free(buffer.data);
			return NULL;
		}

		gdk_pixbuf_loader_close(loader, NULL);
		pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);

		if (pixbuf) {
			g_object_ref(pixbuf);
		}

		g_object_unref(loader);
		free(buffer.data);
	}

	// Verify image is square
	if (pixbuf && !is_square_image(pixbuf)) {
		fprintf(stderr, "Image is not square, rejecting\n");
		g_object_unref(pixbuf);
		return NULL;
	}

	return pixbuf;
}

// Create a minimal dummy menu (required by AppIndicator)
static GtkWidget* create_menu(void) {
	GtkWidget *menu = gtk_menu_new();

	// Add a simple info item
	GtkWidget *info_item = gtk_menu_item_new_with_label("Lyrics - Album Art Display");
	gtk_widget_set_sensitive(info_item, FALSE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), info_item);

	gtk_widget_show_all(menu);

	printf("Minimal tray menu created (album art display only)\n");

	return menu;
}

bool system_tray_init(void) {
	// Initialize GTK if not already initialized
	if (!gtk_init_check(NULL, NULL)) {
		fprintf(stderr, "Failed to initialize GTK\n");
		return false;
	}

	// Initialize CURL globally
	curl_global_init(CURL_GLOBAL_DEFAULT);

	// Create AppIndicator
	indicator = app_indicator_new(
		"lyrics-indicator",
		"audio-player",  // Default icon name
		APP_INDICATOR_CATEGORY_APPLICATION_STATUS
	);

	if (!indicator) {
		fprintf(stderr, "Failed to create AppIndicator\n");
		return false;
	}

	app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
	app_indicator_set_title(indicator, "Lyrics");

	// Create minimal menu (required by AppIndicator)
	menu = create_menu();
	app_indicator_set_menu(indicator, GTK_MENU(menu));

	return true;
}

bool system_tray_update_icon(const char *art_url) {
	if (!indicator || !art_url) {
		fprintf(stderr, "update_icon: indicator=%p, art_url=%p\n", (void*)indicator, (void*)art_url);
		return false;
	}

	printf("Updating tray icon with art URL: %s\n", art_url);

	// Skip if same as last URL
	if (last_art_url && strcmp(last_art_url, art_url) == 0) {
		printf("Same as last URL, skipping update\n");
		return true;
	}

	// Load image from URL
	printf("Loading image from URL...\n");
	GdkPixbuf *pixbuf = load_image_from_url(art_url);

	if (!pixbuf) {
		// Fallback to default icon
		fprintf(stderr, "Failed to load image, using default icon\n");
		app_indicator_set_icon(indicator, "audio-player");
		return false;
	}

	printf("Image loaded successfully\n");

	// Create directory if it doesn't exist
	mkdir(ICON_DIR, 0755);

	// Scale to reasonable size (48x48)
	GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, 48, 48, GDK_INTERP_BILINEAR);
	g_object_unref(pixbuf);

	// Save as PNG (overwrites previous)
	GError *error = NULL;
	if (!gdk_pixbuf_save(scaled, ICON_PATH, "png", &error, NULL)) {
		fprintf(stderr, "Failed to save icon: %s\n", error->message);
		g_error_free(error);
		g_object_unref(scaled);
		return false;
	}

	g_object_unref(scaled);

	printf("Album art saved to: %s\n", ICON_PATH);

	// Set the icon theme path to our directory
	app_indicator_set_icon_theme_path(indicator, ICON_DIR);

	// Update indicator icon using just the name (without extension or path)
	app_indicator_set_icon_full(indicator, ICON_NAME, "Album Art");

	printf("Icon updated: name=%s, theme_path=%s\n", ICON_NAME, ICON_DIR);

	// Update last URL
	free(last_art_url);
	last_art_url = strdup(art_url);

	return true;
}

void system_tray_reset_icon(void) {
	if (!indicator) {
		return;
	}

	printf("Resetting tray icon to default\n");

	// Clear last URL to force reload on next update
	free(last_art_url);
	last_art_url = NULL;

	// Create directory if it doesn't exist
	mkdir(ICON_DIR, 0755);

	// Save default icon to file
	if (!save_default_icon(ICON_PATH)) {
		fprintf(stderr, "Warning: Could not save default icon\n");
		// Fallback: use system icon name without file
		app_indicator_set_icon_theme_path(indicator, NULL);
		app_indicator_set_icon_full(indicator, "audio-player", "Music Player");
		return;
	}

	// Set the icon theme path to our directory
	app_indicator_set_icon_theme_path(indicator, ICON_DIR);

	// Update indicator icon
	app_indicator_set_icon_full(indicator, ICON_NAME, "Music Player");

	printf("Icon reset to default: %s\n", ICON_PATH);
}

void system_tray_update_tooltip(const char *text) {
	if (indicator && text) {
		app_indicator_set_title(indicator, text);
	}
}


void system_tray_update(void) {
	// Process pending GTK events
	while (gtk_events_pending()) {
		gtk_main_iteration();
	}
}

void system_tray_cleanup(void) {
	// Clean up icon file
	unlink(ICON_PATH);

	// Remove icon directory
	rmdir(ICON_DIR);

	free(last_art_url);
	last_art_url = NULL;

	if (indicator) {
		g_object_unref(indicator);
		indicator = NULL;
	}

	// Cleanup CURL
	curl_global_cleanup();
}
