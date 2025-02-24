#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>
#include <pthread.h>

#define MAX_PACKET_SIZE 65527
#define DEFAULT_PACKET_SIZE 56
#define MAX_HOSTS 10

// Add function prototype
uint16_t compute_checksum(uint16_t *addr, int len);

/**
 * Structure to store ping statistics for each target
 * Tracks sent/received packets and timing information
 */
struct ping_stats {
    unsigned long packets_sent;
    unsigned long packets_received;
    double min_rtt;
    double max_rtt;
    double sum_rtt;
    double sum_rtt_square; // For jitter calculation
};

/**
 * Structure to store ping target information
 * Contains hostname, address, and statistics
 */
struct ping_target {
    char *hostname;
    struct sockaddr_in addr;
    struct ping_stats stats;
};

/**
 * Global configuration structure
 * Controls various aspects of ping behavior and display
 */
struct ping_config {
    bool verbose;
    bool quiet;
    int timeout_ms;
    bool show_dns;
    int interval_ms;
    bool continuous;
    bool show_timestamp;
    bool resolve_dns;
};

volatile bool running = true;
GtkWidget *text_view;
struct ping_target targets[MAX_HOSTS];
int num_targets = 0;
int packet_size = DEFAULT_PACKET_SIZE;
struct ping_config config = {
    .verbose = false,
    .quiet = false,
    .timeout_ms = 1000,
    .show_dns = false,
    .interval_ms = 1000,
    .continuous = true,
    .show_timestamp = false,
    .resolve_dns = false
};

// Add these global variables after the existing ones
GtkWidget *entry_host;
GtkWidget *status_bar;
GtkWidget *scroll_window;
pthread_t ping_thread;
bool is_pinging = false;
GtkWidget *start_button;
GtkWidget *stop_button;
GtkWidget *spin_packet_size;
GtkWidget *spin_interval;
GtkWidget *spin_timeout;
GtkWidget *check_continuous;
GtkWidget *check_show_time;
GtkWidget *check_resolve_dns;

// Add this structure for thread-safe text updates
typedef struct {
    char *text;
} TextUpdateData;

// Add new thread-safe append function
static gboolean append_text_idle(gpointer user_data) {
    TextUpdateData *data = (TextUpdateData *)user_data;
    if (data && data->text && GTK_IS_TEXT_VIEW(text_view)) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
        GtkTextIter end;
        
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, data->text, -1);
        
        // Scroll to bottom
        GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, &end, FALSE);
        if (mark) {
            gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(text_view), mark, 0.0, TRUE, 0.0, 1.0);
            gtk_text_buffer_delete_mark(buffer, mark);
        }
    }
    
    if (data) {
        g_free(data->text);
        g_free(data);
    }
    return G_SOURCE_REMOVE;
}

/**
 * Thread-safe function to append text to the GUI text view
 * @param text The text to append
 */
void append_text(const char *text) {
    TextUpdateData *data = g_new(TextUpdateData, 1);
    data->text = g_strdup(text);
    g_idle_add(append_text_idle, data);
}

// Add clear results function
void clear_results(GtkWidget *widget, gpointer data) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buffer, "", -1);
    gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Results cleared");
}

void signal_handler(int signo) {
    if (!config.quiet) {
        append_text("\nReceived SIGINT, stopping...\n");
    }
    running = false;
}

/**
 * Sends a ping packet to the specified target
 * @param sockfd Socket file descriptor
 * @param target Pointer to the ping target structure
 */
void send_ping(int sockfd, struct ping_target *target) {
    char packet[MAX_PACKET_SIZE];
    struct icmphdr *icmp = (struct icmphdr *)packet;
    struct timeval *tv = (struct timeval *)(packet + sizeof(struct icmphdr));

    memset(packet, 0, packet_size);
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->un.echo.id = getpid();
    icmp->un.echo.sequence = ++target->stats.packets_sent;

    gettimeofday(tv, NULL);
    icmp->checksum = compute_checksum((uint16_t *)icmp, packet_size);

    if (sendto(sockfd, packet, packet_size, 0, (struct sockaddr *)&target->addr, sizeof(target->addr)) == -1) {
        append_text("Error sending packet\n");
    }
}

/**
 * Calculates ICMP checksum for packet validation
 * @param addr Pointer to the data
 * @param len Length of the data
 * @return Calculated checksum
 */
uint16_t compute_checksum(uint16_t *addr, int len) {
    uint32_t sum = 0;
    uint16_t *w = addr;

    while (len > 1) {
        sum += *w++;
        len -= 2;
    }

    if (len > 0) {
        sum += *(uint8_t *)w;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

// Update receive_ping function to use thread-safe calls
void receive_ping(int sockfd) {
    char buffer[MAX_PACKET_SIZE];
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);

    int result = recvfrom(sockfd, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr *)&sender, &sender_len);

    if (result < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            append_text("Error receiving packet\n");
        }
        return;
    }

    struct iphdr *iph = (struct iphdr *)buffer;
    struct icmphdr *icmph = (struct icmphdr *)(buffer + (iph->ihl << 2));

    if (icmph->type == ICMP_ECHOREPLY) {
        char msg[256];
        char timestamp[32] = "";
        if (config.verbose) {
            time_t now;
            struct tm *tm_info;
            time(&now);
            tm_info = localtime(&now);
            strftime(timestamp, sizeof(timestamp), "[%H:%M:%S] ", tm_info);
        }
        
        char host[NI_MAXHOST] = "";
        if (config.show_dns) {
            struct sockaddr_in addr = sender;
            getnameinfo((struct sockaddr*)&addr, sizeof(addr),
                       host, sizeof(host), NULL, 0, 0);
        }

        snprintf(msg, sizeof(msg), "%s%s%s%s: ttl=%d\n",
                timestamp,
                "Reply from ",
                config.show_dns ? host : inet_ntoa(sender.sin_addr),
                config.show_dns ? inet_ntoa(sender.sin_addr) : "",
                iph->ttl);
        append_text(msg);
    }
}

/**
 * Main ping loop running in separate thread
 * Handles sending and receiving ping packets
 * @param arg Thread argument (unused)
 * @return NULL
 */
void *ping_loop(void *arg) {
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd == -1) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Error creating socket: %s\n", strerror(errno));
        append_text(error_msg);
        is_pinging = false;
        return NULL;
    }

    struct timeval last_ping, current_time;
    gettimeofday(&last_ping, NULL);

    while (is_pinging && running) {
        for (int i = 0; i < num_targets && is_pinging; i++) {
            // Get current time
            gettimeofday(&current_time, NULL);
            
            // Calculate time elapsed since last ping in milliseconds
            long elapsed_ms = (current_time.tv_sec - last_ping.tv_sec) * 1000 +
                            (current_time.tv_usec - last_ping.tv_usec) / 1000;
            
            // If we haven't waited long enough, sleep for the remaining time
            if (elapsed_ms < config.interval_ms) {
                usleep((config.interval_ms - elapsed_ms) * 1000);
            }
            
            send_ping(sockfd, &targets[i]);
            receive_ping(sockfd);
            
            // Update last ping time
            gettimeofday(&last_ping, NULL);
        }
        
        if (!config.continuous) {
            break;
        }
    }

    close(sockfd);
    return NULL;
}

gboolean update_ui(gpointer data) {
    if (!is_pinging) {
        return FALSE;  // Stop the timeout when pinging stops
    }
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
    return TRUE;
}

void cleanup_ping_thread() {
    if (is_pinging) {
        is_pinging = false;
        pthread_join(ping_thread, NULL);
        gtk_widget_set_sensitive(start_button, TRUE);
        gtk_widget_set_sensitive(stop_button, FALSE);
        gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Ping stopped");
    }
}

void update_config_from_ui() {
    config.timeout_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_timeout));
    config.interval_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_interval));
    packet_size = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_packet_size));
    config.continuous = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_continuous));
    config.show_dns = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_resolve_dns));
    config.verbose = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_show_time));
}

void start_ping(GtkWidget *widget, gpointer data) {
    if (is_pinging) return;

    if (num_targets == 0) {
        gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Please add at least one host");
        return;
    }

    if (geteuid() != 0) {
        append_text("Error: This program requires root privileges.\n");
        gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Error: Root privileges required");
        return;
    }

    update_config_from_ui();

    is_pinging = true;
    gtk_widget_set_sensitive(start_button, FALSE);
    gtk_widget_set_sensitive(stop_button, TRUE);
    gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Pinging started...");

    // Start UI update timeout
    g_timeout_add(100, update_ui, NULL);

    // Start ping thread
    if (pthread_create(&ping_thread, NULL, ping_loop, NULL) != 0) {
        append_text("Error creating ping thread\n");
        is_pinging = false;
        gtk_widget_set_sensitive(start_button, TRUE);
        gtk_widget_set_sensitive(stop_button, FALSE);
    }
}

void stop_ping(GtkWidget *widget, gpointer data) {
    cleanup_ping_thread();
}

void add_host(GtkWidget *widget, gpointer data) {
    const char *host = gtk_entry_get_text(GTK_ENTRY(entry_host));
    if (strlen(host) == 0) {
        gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Please enter a hostname or IP address");
        return;
    }
    if (num_targets >= MAX_HOSTS) {
        gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Maximum number of hosts reached");
        return;
    }

    struct hostent *he;
    struct ping_target *target = &targets[num_targets];
    target->hostname = strdup(host);
    
    if ((he = gethostbyname(host)) == NULL) {
        gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Invalid hostname");
        free(target->hostname);
        return;
    }

    memcpy(&target->addr.sin_addr, he->h_addr_list[0], he->h_length);
    target->addr.sin_family = AF_INET;
    target->stats = (struct ping_stats){0};
    num_targets++;

    char msg[256];
    snprintf(msg, sizeof(msg), "Added target: %s\n", host);
    append_text(msg);
    gtk_entry_set_text(GTK_ENTRY(entry_host), "");
    gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Host added successfully");
}

// Update main function to initialize GTK threading
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // Set dark theme
    GtkSettings *settings = gtk_settings_get_default();
    g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Fping");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    // Input frame
    GtkWidget *input_frame = gtk_frame_new("Host Configuration");
    gtk_box_pack_start(GTK_BOX(main_box), input_frame, FALSE, FALSE, 0);

    GtkWidget *input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(input_box), 5);
    gtk_container_add(GTK_CONTAINER(input_frame), input_box);

    // Host entry
    entry_host = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_host), "Enter hostname or IP address");
    gtk_box_pack_start(GTK_BOX(input_box), entry_host, TRUE, TRUE, 0);

    // Add host button
    GtkWidget *add_button = gtk_button_new_with_label("Add Host");
    gtk_box_pack_start(GTK_BOX(input_box), add_button, FALSE, FALSE, 0);

    // Modify the buttons section
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(input_box), button_box, FALSE, FALSE, 0);

    start_button = gtk_button_new_with_label("Start Ping");
    stop_button = gtk_button_new_with_label("Stop Ping");
    gtk_widget_set_sensitive(stop_button, FALSE);

    gtk_box_pack_start(GTK_BOX(button_box), start_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), stop_button, FALSE, FALSE, 0);

    // Add clear button to button box
    GtkWidget *clear_button = gtk_button_new_with_label("Clear Results");
    gtk_box_pack_start(GTK_BOX(button_box), clear_button, FALSE, FALSE, 0);

    // Add configuration frame after input frame
    GtkWidget *config_frame = gtk_frame_new("Ping Configuration");
    gtk_box_pack_start(GTK_BOX(main_box), config_frame, FALSE, FALSE, 5);  // Add padding

    GtkWidget *config_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(config_grid), 10);       // Increase row spacing
    gtk_grid_set_column_spacing(GTK_GRID(config_grid), 20);    // Increase column spacing
    gtk_container_set_border_width(GTK_CONTAINER(config_grid), 10);
    gtk_container_add(GTK_CONTAINER(config_frame), config_grid);

    // Make labels left-aligned
    GtkWidget *label_packet = gtk_label_new("Packet Size (bytes):");
    gtk_widget_set_halign(label_packet, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(config_grid), label_packet, 0, 0, 1, 1);
    
    spin_packet_size = gtk_spin_button_new_with_range(32, MAX_PACKET_SIZE, 1);
    gtk_widget_set_hexpand(spin_packet_size, TRUE);  // Allow horizontal expansion
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_packet_size), DEFAULT_PACKET_SIZE);
    gtk_grid_attach(GTK_GRID(config_grid), spin_packet_size, 1, 0, 1, 1);

    GtkWidget *label_interval = gtk_label_new("Interval (ms):");
    gtk_widget_set_halign(label_interval, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(config_grid), label_interval, 0, 1, 1, 1);
    
    spin_interval = gtk_spin_button_new_with_range(100, 5000, 100);
    gtk_widget_set_hexpand(spin_interval, TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_interval), 1000);
    gtk_grid_attach(GTK_GRID(config_grid), spin_interval, 1, 1, 1, 1);

    GtkWidget *label_timeout = gtk_label_new("Timeout (ms):");
    gtk_widget_set_halign(label_timeout, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(config_grid), label_timeout, 0, 2, 1, 1);
    
    spin_timeout = gtk_spin_button_new_with_range(100, 5000, 100);
    gtk_widget_set_hexpand(spin_timeout, TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_timeout), 1000);
    gtk_grid_attach(GTK_GRID(config_grid), spin_timeout, 1, 2, 1, 1);

    // Add separator between spinbuttons and checkboxes
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_grid_attach(GTK_GRID(config_grid), separator, 2, 0, 1, 3);

    // Move checkboxes to their own column with proper spacing
    GtkWidget *check_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_grid_attach(GTK_GRID(config_grid), check_box, 3, 0, 1, 3);

    check_continuous = gtk_check_button_new_with_label("Continuous Ping");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_continuous), TRUE);
    gtk_box_pack_start(GTK_BOX(check_box), check_continuous, FALSE, FALSE, 0);

    check_show_time = gtk_check_button_new_with_label("Show Timestamp");
    gtk_box_pack_start(GTK_BOX(check_box), check_show_time, FALSE, FALSE, 0);

    check_resolve_dns = gtk_check_button_new_with_label("Resolve DNS");
    gtk_box_pack_start(GTK_BOX(check_box), check_resolve_dns, FALSE, FALSE, 0);

    // Output frame
    GtkWidget *output_frame = gtk_frame_new("Ping Results");
    gtk_box_pack_start(GTK_BOX(main_box), output_frame, TRUE, TRUE, 0);

    // Scrolled window for text view
    scroll_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_window),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(output_frame), scroll_window);

    // Text view
    text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scroll_window), text_view);

    // Status bar
    status_bar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(main_box), status_bar, FALSE, FALSE, 0);
    gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Ready");

    // Modify text view for dark theme colors
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "textview {"
        "   color: #ffffff;"
        "   background-color: #2d2d2d;"
        "}"
        "frame {"
        "   border-color: #1e1e1e;"
        "}"
        "button {"
        "   background-image: none;"
        "   background-color: #3d3d3d;"
        "   color: #ffffff;"
        "   border-color: #1e1e1e;"
        "}"
        "entry {"
        "   background-color: #2d2d2d;"
        "   color: #ffffff;"
        "}"
        "spinbutton {"
        "   background-color: #2d2d2d;"
        "   color: #ffffff;"
        "}"
        "checkbutton {"
        "   color: #ffffff;"
        "}", -1, NULL);

    GtkStyleContext *context = gtk_widget_get_style_context(window);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                            GTK_STYLE_PROVIDER(provider),
                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Connect signals
    g_signal_connect(window, "destroy", G_CALLBACK(cleanup_ping_thread), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(add_button, "clicked", G_CALLBACK(add_host), NULL);
    g_signal_connect(start_button, "clicked", G_CALLBACK(start_ping), NULL);
    g_signal_connect(stop_button, "clicked", G_CALLBACK(stop_ping), NULL);
    g_signal_connect(entry_host, "activate", G_CALLBACK(add_host), NULL);
    g_signal_connect(start_button, "clicked", G_CALLBACK(update_config_from_ui), NULL);
    g_signal_connect(clear_button, "clicked", G_CALLBACK(clear_results), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
