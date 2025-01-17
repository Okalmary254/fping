#include <gtk/gtk.h>
#include <stdio.h>
#include <conio.h>
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

#define MAX_PACKET_SIZE 65527
#define DEFAULT_PACKET_SIZE 56
#define MAX_HOSTS 10

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct ping_stats {
    unsigned long packets_sent;
    unsigned long packets_received;
    double min_rtt;
    double max_rtt;
    double sum_rtt;
    double sum_rtt_square; // For jitter calculation
};

struct ping_target {
    char *hostname;
    struct sockaddr_in addr;
    struct ping_stats stats;
};

struct ping_config {
    bool verbose;
    bool quiet;
    int timeout_ms;
    bool show_dns;
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
    .show_dns = false
};

void append_text(const char *text) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text, -1);
}

void signal_handler(int signo) {
    if (!config.quiet) {
        append_text("\nReceived SIGINT, stopping...\n");
    }
    running = false;
}

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
        snprintf(msg, sizeof(msg), "Reply from %s: ttl=%d\n", inet_ntoa(sender.sin_addr), iph->ttl);
        append_text(msg);
    }
}

void start_ping(GtkWidget *widget, gpointer data) {
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd == -1) {
        append_text("Error creating socket\n");
        return;
    }

    while (running) {
        for (int i = 0; i < num_targets; i++) {
            send_ping(sockfd, &targets[i]);
            receive_ping(sockfd);
        }
        sleep(1);
    }

    close(sockfd);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Ping Tool");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), text_view, TRUE, TRUE, 0);

    GtkWidget *button = gtk_button_new_with_label("Start Ping");
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(button, "clicked", G_CALLBACK(start_ping), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
