#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>
#include <getopt.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <netdb.h>
#include <pthread.h>

#ifndef INADDR_ANY
#define INADDR_ANY  0
#endif

#ifndef HAVE_X11
#define HAVE_X11	0
#endif
#if HAVE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

unsigned int read32 ( unsigned int );

unsigned int read_register ( unsigned int );

#define ROM_START       0x00000000
#define RAM_START       0x40000000
#define PERIPH_START    0xE0000000
#define ESP_AT_TX       0xE0000040
#define ESP_AT_RX       0xE0000044
#define ESP_AT_STATUS   0xE0000048
#define HOST_SECONDS    0xE000004C

static int DBUGFETCH = 0;
static int DBUGRAM   = 0;
static int DBUGRAMW  = 0;
static int DBUGREG   = 0;
static int DBUG      = 0;
static int DBUGUART  = 0;
static int DBUGSTACK = 0;
static int DBUGPC    = 0;
static int DISS      = 0;
static int DISR      = 0;

#define MINUS_ONE   ((unsigned int)(~0))

#define ROMADDMASK 0xFFFFF
#define RAMADDMASK 0xFFFFF

#define ROMSIZE (ROMADDMASK+1)
#define RAMSIZE (RAMADDMASK+1)

unsigned short rom[ROMSIZE>>1];
unsigned short ram[RAMSIZE>>1];

#define CPSR_N (1<<31)
#define CPSR_Z (1<<30)
#define CPSR_C (1<<29)
#define CPSR_V (1<<28)
#define CPSR_Q (1<<27)
#define XPSR_FLAGS 0xF8000000u
#define XPSR_THUMB (1u<<24)
#define XPSR_STACK_ALIGN (1u<<9)

#define EXCEPTION_HARDFAULT 3
#define EXCEPTION_SVC 11
#define EXCEPTION_SYSTICK 15

#define CONTROL_SPSEL 2u
#define EXC_RETURN_HANDLER 0xFFFFFFF1u
#define EXC_RETURN_THREAD_MSP 0xFFFFFFF9u
#define EXC_RETURN_THREAD_PSP 0xFFFFFFFDu
#define SHPR2 0xE000ED1Cu
#define SHPR3 0xE000ED20u

unsigned int vcdcount;
unsigned int output_vcd = 0;
FILE *fpvcd;

#define SYSTICK_ENABLE      0x00000001
#define SYSTICK_TICKINT     0x00000002
#define SYSTICK_CLKSOURCE   0x00000004
#define SYSTICK_COUNTFLAG   0x00010000
#define SYSTICK_MASK        (SYSTICK_COUNTFLAG | SYSTICK_CLKSOURCE | SYSTICK_TICKINT | SYSTICK_ENABLE)

#define CALIB_TENMS         0x00FFFFFF
#define CALIB_SKEW          0x40000000
#define CALIB_NOREF         0x80000000

unsigned int systick_ctrl;
unsigned int systick_reload;
unsigned int systick_count;
unsigned int systick_calibrate;
static int systick_pending;
static int primask;
static unsigned int psp, control;
static unsigned int shpr2, shpr3;

void handle_disk(unsigned int wr);

#define MAX_DISK    4
unsigned int disk_count;
int disk_fd[MAX_DISK];
off_t disk_size[MAX_DISK];
unsigned int disk_status;
unsigned int disk_buffer[128];
unsigned int disk_offset_low, disk_offset_high;
unsigned int disk_ide_id;

unsigned int *frame_buffer;
unsigned int fb_width, fb_height;
unsigned int fb_port;

unsigned int halfadd;
unsigned int cpsr;
unsigned int active_exception;
unsigned int reg_norm[16]; // r13 stores MSP

static int using_psp(void)
{
    return !active_exception && (control & CONTROL_SPSEL);
}

unsigned int reg_event; //event register
typedef enum {NoWait=0,WaitForEvt,WaitForInt} WaitType;
WaitType waiting; //wait for

unsigned int cpuhz;
unsigned int cpuid;
unsigned int entry = 0;
const char *output_file_name;

#define THOR_MAXINPUT   (64 * 1024)

int read_fd, write_fd, error_fd;
unsigned char input_buffer[THOR_MAXINPUT];
typedef enum {EvtKey, EvtButton} EvtType;
typedef struct {
    EvtType typ;
    int X, Y;
} INPUT_EVENT;
INPUT_EVENT input_event[THOR_MAXINPUT];
size_t input_read_ptr = 0, input_write_ptr = 0;
int socket_fd = -1;

unsigned long clks;
unsigned long instructions;
unsigned long fetches;
unsigned long reads;
unsigned long writes;
unsigned long systick_ints;

#define MAX_SYM 4096
unsigned int symaddr[4096];
char *symbols[4096];

#if HAVE_X11
Display *xDisplay;
Window xWin;
XImage *xImg;
int xScreenNum, imgWidth, imgHeight;
#endif
/* POSIX socket transport for the ESP-AT UART, adapted from
   Oberon07-tools/lib/TCPSockets.c. Only DNS runs on worker threads. */
#define BAD_SOCKET (-1)
static pthread_mutex_t resolver_lock = PTHREAD_MUTEX_INITIALIZER;
#define ESP_LOCK() ((void)pthread_mutex_lock(&resolver_lock))
#define ESP_UNLOCK() ((void)pthread_mutex_unlock(&resolver_lock))
static int socket_error(void) { return errno; }
static int busy(int e) { return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS || e == EALREADY || e == EINTR; }
static uint32_t esp_milliseconds(void) {
    struct timespec ts = {0};
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
static int nonblocking(int s) {
    int flags = fcntl(s, F_GETFL);
    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    /* Do not inherit sockets across exec. */
    flags = fcntl(s, F_GETFD);
    if (flags >= 0) (void)fcntl(s, F_SETFD, flags | FD_CLOEXEC);
    return 0;
}

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK (0x7f000001u)
#endif

#define ESP_LINKS 5
#define ESP_CONNECT_TIMEOUT 10000u
#define ESP_PAYLOAD_MAX 1460
typedef enum { ESP_CLOSED, ESP_RESOLVING, ESP_CONNECTING, ESP_TCP, ESP_UDP } EspPhase;
/* resolver state: 0 idle, 1 running, 2 finished, 3 abandoned.
   A canceled running resolver retains its slot until getaddrinfo returns. */
typedef struct {
    int resolver_state, resolver_error;
    char host[256], service[8];
    struct addrinfo *addresses, *next;
    int socket;
    EspPhase phase;
    uint32_t started;
    struct sockaddr_in peer;
    int accepted;
    int udp_mode; /* ESP-AT peer update policy */
} Link;
static Link links[ESP_LINKS];
static int sockets_initialized;
static int listener = BAD_SOCKET;

static void esp_resolve(Link *p)
{
    struct addrinfo hints, *addresses = NULL;
    int error;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    error = getaddrinfo(p->host, p->service, &hints, &addresses);
    ESP_LOCK();
    if (p->resolver_state == 3) {
        if (addresses) freeaddrinfo(addresses);
        p->resolver_state = 0;
    } else {
        p->addresses = addresses;
        p->resolver_error = error;
        p->resolver_state = 2;
    }
    ESP_UNLOCK();
}
static void *resolver_thread(void *arg) { esp_resolve((Link *)arg); return NULL; }

static void esp_socket_close(int id)
{
    Link *p;
    if (!sockets_initialized || id < 0 || id >= ESP_LINKS) return;
    p = &links[id];
    if (p->socket != BAD_SOCKET) close(p->socket);
    p->socket = BAD_SOCKET;
    p->phase = ESP_CLOSED;
    p->accepted = 0;
    ESP_LOCK();
    if (p->resolver_state == 1) p->resolver_state = 3;
    else if (p->resolver_state != 3) {
        if (p->addresses) freeaddrinfo(p->addresses);
        p->addresses = p->next = NULL;
        p->resolver_state = 0;
    }
    ESP_UNLOCK();
}

static void esp_socket_stop_listener(void)
{
    int i;
    if (listener != BAD_SOCKET) close(listener);
    listener = BAD_SOCKET;
    for (i = 0; i < ESP_LINKS; ++i) {
        if (links[i].accepted) esp_socket_close(i);
    }
}

static int esp_socket_listen(int port)
{
    struct sockaddr_in address;
    int s;
    if (port < 1 || port > 65535 || listener != BAD_SOCKET) return -1;
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD_SOCKET) return -1;
    { int one = 1; (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); }
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t)port);
    if (nonblocking(s) != 0 ||
        bind(s, (const struct sockaddr *)&address, sizeof address) != 0 ||
        listen(s, 1) != 0) {
        close(s);
        return -1;
    }
    listener = s;
    return 0;
}

static int esp_socket_accept(void)
{
    int i, id = -1;
    int s;
    if (listener == BAD_SOCKET) return -1;
    for (i = 0; i < ESP_LINKS; ++i) {
        if (links[i].accepted) return -1;
        if (id < 0 && links[i].phase == ESP_CLOSED) id = i;
    }
    if (id < 0) return -1;
    s = accept(listener, NULL, NULL);
    if (s == BAD_SOCKET) return -1;
    if (nonblocking(s) != 0) { close(s); return -1; }
#ifdef SO_NOSIGPIPE
    { int one = 1; (void)setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one); }
#endif
    links[id].socket = s;
    links[id].phase = ESP_TCP;
    links[id].accepted = 1;
    return id;
}

static void esp_socket_close_all(void)
{
    int i;
    esp_socket_stop_listener();
    for (i = 0; i < ESP_LINKS; ++i) esp_socket_close(i);
}

static int esp_socket_start(int id, const char *host, int port)
{
    Link *p;
    size_t n;
    int result = 0;
    pthread_t thread;
    pthread_attr_t attr;
    if (id < 0 || id >= ESP_LINKS || port < 1 || port > 65535) return -1;
    n = strlen(host);
    if (!n || n >= sizeof links[0].host) return -1;
    p = &links[id];
    if (p->phase) return -1;
    ESP_LOCK();
    if (p->resolver_state != 0) { ESP_UNLOCK(); return -1; }
    memcpy(p->host, host, n + 1);
    snprintf(p->service, sizeof p->service, "%d", port);
    p->resolver_state = 1;
    p->resolver_error = 0;
    p->started = esp_milliseconds();
    p->phase = ESP_RESOLVING;
    if (pthread_attr_init(&attr) != 0) result = -1;
    else {
        if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0 ||
            pthread_create(&thread, &attr, resolver_thread, p) != 0) result = -1;
        (void)pthread_attr_destroy(&attr);
    }
    if (result) { p->resolver_state = 0; p->phase = ESP_CLOSED; }
    ESP_UNLOCK();
    return result;
}

static void release_addresses(Link *p)
{
    ESP_LOCK();
    if (p->addresses) freeaddrinfo(p->addresses);
    p->addresses = p->next = NULL;
    p->resolver_state = 0;
    ESP_UNLOCK();
}

static int try_address(Link *p)
{
    int result, error;
    struct addrinfo *a;
    while ((a = p->next) != NULL) {
        p->next = a->ai_next;
        p->socket = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (p->socket == BAD_SOCKET) continue;
        if (p->socket >= FD_SETSIZE) { close(p->socket); p->socket = BAD_SOCKET; continue; }
#ifdef SO_NOSIGPIPE
        { int one = 1; (void)setsockopt(p->socket, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one); }
#endif
        if (nonblocking(p->socket) == 0) {
            result = connect(p->socket, a->ai_addr, a->ai_addrlen);
            if (result == 0) { p->phase = ESP_TCP; release_addresses(p); return 1; }
            error = socket_error();
            if (busy(error)) { p->phase = ESP_CONNECTING; return 0; }
        }
        close(p->socket);
        p->socket = BAD_SOCKET;
    }
    release_addresses(p);
    p->phase = ESP_CLOSED;
    return -1;
}

static int esp_socket_poll(int id)
{
    Link *p;
    fd_set wr, ex;
    struct timeval zero;
    int state, result, error = 0;
    socklen_t length = sizeof error;
    if (id < 0 || id >= ESP_LINKS) return -1;
    p = &links[id];
    if (p->phase == ESP_TCP) return 1;
    if (!p->phase) return -1;
    if ((uint32_t)(esp_milliseconds() - p->started) >= ESP_CONNECT_TIMEOUT) {
        esp_socket_close(id); return -1;
    }
    if (p->phase == ESP_RESOLVING) {
        ESP_LOCK(); state = p->resolver_state; ESP_UNLOCK();
        if (state != 2) return 0;
        if (p->resolver_error) { esp_socket_close(id); return -1; }
        p->next = p->addresses;
        return try_address(p);
    }
    FD_ZERO(&wr); FD_ZERO(&ex);
    FD_SET(p->socket, &wr); FD_SET(p->socket, &ex);
    zero.tv_sec = 0; zero.tv_usec = 0;
    result = select(p->socket + 1, NULL, &wr, &ex, &zero);
    if (result == 0 || (result < 0 && busy(socket_error()))) return 0;
    if (result > 0 && getsockopt(p->socket, SOL_SOCKET, SO_ERROR, (char *)&error, &length) == 0 && !error) {
        p->phase = ESP_TCP; release_addresses(p); return 1;
    }
    close(p->socket); p->socket = BAD_SOCKET;
    return try_address(p);
}

static int esp_socket_send(int id, const unsigned char *data, int offset, int count)
{
    int n, flags = 0;
    if (id < 0 || id >= ESP_LINKS || links[id].phase != ESP_TCP || offset < 0 || count < 0 || count > ESP_PAYLOAD_MAX || offset > ESP_PAYLOAD_MAX - count) return -1;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    n = (int)send(links[id].socket, (const char *)data + offset, count, flags);
    if (n < 0) return busy(socket_error()) ? 0 : -1;
    return n;
}

static int esp_socket_receive(int id, unsigned char *data, int count)
{
    int n;
    if (id < 0 || id >= ESP_LINKS || links[id].phase != ESP_TCP || count < 1 || count > 512) return -1;
    n = (int)recv(links[id].socket, (char *)data, count, 0);
    if (n < 0) return busy(socket_error()) ? 0 : -1;
    return n == 0 ? -1 : n;
}

static int esp_ipv4(struct sockaddr_in *address, const char *host, int port)
{
    memset(address, 0, sizeof *address);
    address->sin_family = AF_INET;
    address->sin_port = htons((uint16_t)port);
    return inet_pton(AF_INET, host, &address->sin_addr) == 1;
}

static int esp_socket_udp_start(int id, const char *host, int port, int local, int mode)
{
    Link *p;
    int s;
    struct sockaddr_in address, peer;
    int one = 1;
    if (id < 0 || id >= ESP_LINKS || port < 1 || port > 65535 ||
        local < 0 || local > 65535 || mode < 0 || mode > 2 || !esp_ipv4(&peer, host, port)) return -1;
    p = &links[id];
    if (p->phase) return -1;
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == BAD_SOCKET) return -1;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)local);
    if (nonblocking(s) != 0 ||
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof one) != 0 ||
        bind(s, (const struct sockaddr *)&address, sizeof address) != 0) {
        close(s);
        return -1;
    }
    p->socket = s;
    p->peer = peer;
    p->udp_mode = mode;
    p->phase = ESP_UDP;
    return 0;
}

static int esp_socket_udp_send(int id, const unsigned char *data, int count, const char *host, int port)
{
    struct sockaddr_in peer;
    int n;
    if (id < 0 || id >= ESP_LINKS || links[id].phase != ESP_UDP || count < 1 || count > ESP_PAYLOAD_MAX) return -1;
    peer = links[id].peer;
    if (host[0] && (port < 1 || port > 65535 || !esp_ipv4(&peer, host, port))) return -1;
    n = (int)sendto(links[id].socket, (const char *)data, count, 0,
                    (const struct sockaddr *)&peer, sizeof peer);
    if (n < 0) return busy(socket_error()) ? 0 : -1;
    return n == count ? 1 : -1;
}

static int esp_socket_udp_receive(int id, unsigned char *data, unsigned char *peer)
{
    struct sockaddr_in source;
    unsigned char packet[ESP_PAYLOAD_MAX + 1];
    int n, error;
    Link *p;
    socklen_t length = sizeof source;
    if (id < 0 || id >= ESP_LINKS || links[id].phase != ESP_UDP) return -1;
    p = &links[id];
    n = (int)recvfrom(p->socket, (char *)packet, sizeof packet, 0,
                      (struct sockaddr *)&source, &length);
    if (n < 0) {
        error = socket_error();
        if (error == ECONNREFUSED) return 0;
        return busy(error) ? 0 : -1;
    }
    if (n > ESP_PAYLOAD_MAX) return 0;
    if (p->udp_mode > 0) {
        if (p->udp_mode == 1 && (source.sin_addr.s_addr != p->peer.sin_addr.s_addr ||
                                source.sin_port != p->peer.sin_port)) p->udp_mode = 0;
        p->peer = source;
    }
    memcpy(data, packet, (size_t)n);
    memcpy(peer, &source.sin_addr.s_addr, 4);
    memcpy(peer + 4, &source.sin_port, 2);
    return n + 1;
}

static void esp_sockets_init(void)
{
    int i;
    if (sockets_initialized) return;
    for (i = 0; i < ESP_LINKS; ++i) links[i].socket = BAD_SOCKET;
    sockets_initialized = 1;
    (void)atexit(esp_socket_close_all);
}

/* ESP-AT UART protocol, ported from Oberon07-tools/Oberon0/TCPNet.Mod. */
enum { ESP_RX_SIZE = 8192, ESP_REPLY_SPACE = 1024, ESP_TCP_READ = 512 };
static struct {
    unsigned char rx[ESP_RX_SIZE];
    size_t head, count;
    char line[512];
    size_t length;
    bool echo, joined, discard, ip_info;
    bool connected[ESP_LINKS], udp[ESP_LINKS], accepted[ESP_LINKS];
    int opening, sending, wanted, collected, sent;
    unsigned char payload[ESP_PAYLOAD_MAX];
    char send_host[256];
    int send_port;
} esp;

static size_t esp_room(void)
{
    return sizeof esp.rx - esp.count;
}

static void esp_put(unsigned char ch)
{
    assert(esp_room() != 0);
    esp.rx[(esp.head + esp.count) % sizeof esp.rx] = ch;
    ++esp.count;
}

static void esp_text(const char *text)
{
    while (*text) esp_put(*text++);
}

static void esp_number(int number)
{
    char digits[12];
    snprintf(digits, sizeof digits, "%d", number);
    esp_text(digits);
}

static void esp_reply(bool ok)
{
    esp_text(ok ? "\r\nOK\r\n" : "\r\nERROR\r\n");
}

static void esp_event(int id, const char *event)
{
    esp_text("\r\n");
    esp_number(id);
    esp_put(',');
    esp_text(event);
    esp_text("\r\n");
}

static bool esp_match(const char **input, const char *prefix)
{
    size_t length = strlen(prefix);

    if (strncmp(*input, prefix, length) != 0) return false;
    *input += length;
    return true;
}

static void esp_skip(const char **input)
{
    while (**input == ' ') ++*input;
}

static bool esp_comma(const char **input)
{
    esp_skip(input);
    return esp_match(input, ",");
}

static bool esp_end(const char *input)
{
    esp_skip(&input);
    return *input == '\0';
}

static bool esp_decimal(const char **input, int *value)
{
    const char *start;
    int number = 0;

    esp_skip(input);
    start = *input;
    while (**input >= '0' && **input <= '9') {
        number = number * 10 + **input - '0';
        if (number > 65535) return false;
        ++*input;
    }
    *value = number;
    return *input != start;
}

static bool esp_quoted(const char **input, char *value, size_t capacity)
{
    size_t length = 0;

    esp_skip(input);
    if (!esp_match(input, "\"")) return false;
    while (**input && **input != '"') {
        unsigned char ch = *(*input)++;
        if (ch == '\\' && **input) ch = *(*input)++;
        if (ch < ' ' || length + 1 >= capacity) return false;
        value[length++] = ch;
    }
    value[length] = '\0';
    return esp_match(input, "\"");
}

static void esp_disconnect(int id)
{
    esp_socket_close(id);
    esp.connected[id] = esp.accepted[id] = false;
    esp_event(id, "CLOSED");
}

static void esp_close(void)
{
    esp_socket_close_all();
    memset(esp.connected, 0, sizeof esp.connected);
    memset(esp.udp, 0, sizeof esp.udp);
    memset(esp.accepted, 0, sizeof esp.accepted);
    esp.opening = esp.sending = -1;
}

static void esp_reset(void)
{
    esp_close();
    esp.head = esp.count = esp.length = 0;
    esp.echo = esp.joined = true;
    esp.discard = esp.ip_info = false;
}

static void esp_connect(const char *input)
{
    int id, port, local = 0, mode = 0;
    char protocol[4], host[256];

    if (!esp_decimal(&input, &id) || !esp_comma(&input) ||
        !esp_quoted(&input, protocol, sizeof protocol) || !esp_comma(&input) ||
        !esp_quoted(&input, host, sizeof host) || !esp_comma(&input) ||
        !esp_decimal(&input, &port) || !port || id >= ESP_LINKS ||
        !esp.joined || esp.connected[id]) {
        esp_reply(false);
        return;
    }

    if (!strcmp(protocol, "TCP") && esp_end(input) &&
        esp_socket_start(id, host, port) == 0) {
        esp.opening = id;
        esp.udp[id] = false;
        return;
    }

    if (!strcmp(protocol, "UDP") &&
        (esp_end(input) ||
         (esp_comma(&input) && esp_decimal(&input, &local) &&
          (esp_end(input) || (esp_comma(&input) && esp_decimal(&input, &mode))))) &&
        esp_end(input) && esp_socket_udp_start(id, host, port, local, mode) == 0) {
        esp.udp[id] = esp.connected[id] = true;
        esp_event(id, "CONNECT");
        esp_reply(true);
        return;
    }

    esp_reply(false);
}

static void esp_begin_send(const char *input)
{
    int id, count;

    esp.send_host[0] = '\0';
    esp.send_port = 0;
    if (!esp_decimal(&input, &id) || !esp_comma(&input) ||
        !esp_decimal(&input, &count) || id >= ESP_LINKS ||
        !esp.connected[id] || count < 1 || count > ESP_PAYLOAD_MAX ||
        (!esp_end(input) &&
         (!esp.udp[id] || !esp_comma(&input) ||
          !esp_quoted(&input, esp.send_host, sizeof esp.send_host) ||
          !esp.send_host[0] || !esp_comma(&input) ||
          !esp_decimal(&input, &esp.send_port) || !esp.send_port ||
          !esp_end(input)))) {
        esp_reply(false);
        return;
    }
    esp.sending = id;
    esp.wanted = count;
    esp.collected = esp.sent = 0;
    esp_reply(true);
    esp_text("> ");
}

static void esp_execute(void)
{
    static const char *const setup[] = {
        "AT", "AT+CWMODE=1", "AT+CIPMODE=0", "AT+CIPMUX=1",
        "AT+CIPRECVMODE=0", "AT+CIPSERVERMAXCONN=1"
    };
    const char *input = esp.line;
    int id, port;

    esp.line[esp.length] = '\0';
    esp.length = 0;
    if (esp.echo) {
        esp_text(input);
        esp_text("\r\n");
    }
    for (size_t i = 0; i < sizeof setup / sizeof *setup; ++i) {
        if (!strcmp(input, setup[i])) {
            esp_reply(true);
            return;
        }
    }

    if (!strcmp(input, "ATE0") || !strcmp(input, "ATE1")) {
        esp.echo = input[3] == '1';
        esp_reply(true);
    } else if (!strcmp(input, "AT+CIPDINFO=0") || !strcmp(input, "AT+CIPDINFO=1")) {
        esp.ip_info = input[12] == '1';
        esp_reply(true);
    } else if (!strcmp(input, "AT+CIPSERVER=0,1")) {
        for (id = 0; id < ESP_LINKS; ++id)
            if (esp.accepted[id]) esp_disconnect(id);
        esp_socket_stop_listener();
        esp_reply(true);
    } else if (!strcmp(input, "AT+RST")) {
        esp_reset();
        esp_reply(true);
        esp_text("ready\r\n");
    } else if (!strcmp(input, "AT+CWQAP")) {
        for (id = 0; id < ESP_LINKS; ++id)
            if (esp.connected[id]) esp_event(id, "CLOSED");
        esp_close();
        esp.joined = false;
        esp_text("WIFI DISCONNECT\r\n");
        esp_reply(true);
    } else if (esp_match(&input, "AT+CWJAP=")) {
        char ssid[256], password[256];
        bool ok = esp_quoted(&input, ssid, sizeof ssid) && esp_comma(&input) &&
                  esp_quoted(&input, password, sizeof password) && esp_end(input);
        if (ok) {
            esp.joined = true;
            esp_text("WIFI CONNECTED\r\nWIFI GOT IP\r\n");
        }
        esp_reply(ok);
    } else if (esp_match(&input, "AT+CIPSERVER=1,")) {
        esp_reply(esp_decimal(&input, &port) && esp_end(input) && esp.joined &&
                  esp_socket_listen(port) == 0);
    } else if (esp_match(&input, "AT+CIPSTART=")) {
        esp_connect(input);
    } else if (esp_match(&input, "AT+CIPSEND=")) {
        esp_begin_send(input);
    } else if (esp_match(&input, "AT+CIPCLOSE=")) {
        bool ok = esp_decimal(&input, &id) && id < ESP_LINKS &&
                  esp_end(input) && esp.connected[id];
        if (ok) esp_disconnect(id);
        esp_reply(ok);
    } else {
        esp_reply(false);
    }
}

static void esp_flush_send(void)
{
    int id = esp.sending, result;

    if (!esp.connected[id]) {
        result = -1;
    } else if (esp.udp[id]) {
        result = esp_socket_udp_send(id, esp.payload, esp.wanted,
                                     esp.send_host, esp.send_port);
    } else {
        result = esp_socket_send(id, esp.payload, esp.sent, esp.wanted - esp.sent);
        if (result < 0) {
            esp_disconnect(id);
        } else {
            esp.sent += result;
            result = esp.sent == esp.wanted;
        }
    }
    if (result != 0) {
        esp_text(result > 0 ? "SEND OK\r\n" : "SEND FAIL\r\n");
        esp.sending = -1;
    }
}

static void esp_receive(int id)
{
    unsigned char data[ESP_PAYLOAD_MAX], peer[6];
    bool udp = esp.udp[id];
    int size;

    if (esp_room() < ESP_REPLY_SPACE + (udp ? ESP_PAYLOAD_MAX : ESP_TCP_READ) + 64)
        return;
    if (udp) {
        size = esp_socket_udp_receive(id, data, peer);
    } else {
        size = esp_socket_receive(id, data, ESP_TCP_READ);
    }

    if (size < 0) {
        esp_disconnect(id);
        if (esp.sending == id) {
            esp_text("SEND FAIL\r\n");
            esp.sending = -1;
        }
        return;
    }
    if (!size) return;
    if (udp) --size; /* UDP transport represents an empty datagram as 1. */

    esp_text("\r\n+IPD,");
    esp_number(id);
    esp_put(',');
    esp_number(size);
    if (udp && esp.ip_info) {
        esp_text(",\"");
        for (int i = 0; i < 4; ++i) {
            if (i) esp_put('.');
            esp_number(peer[i]);
        }
        esp_text("\",");
        esp_number(256 * peer[4] + peer[5]);
    }
    esp_put(':');
    for (int i = 0; i < size; ++i) esp_put(data[i]);
}

static void esp_cycle(void)
{
    if (esp_room() >= ESP_REPLY_SPACE) {
        int id = esp_socket_accept();
        if (id >= 0) {
            esp.connected[id] = esp.accepted[id] = true;
            esp.udp[id] = false;
            esp_event(id, "CONNECT");
        }
        if (esp.opening >= 0) {
            id = esp.opening;
            int result = esp_socket_poll(id);
            if (result > 0) {
                esp.connected[id] = true;
                esp_event(id, "CONNECT");
                esp_reply(true);
            } else if (result < 0) {
                esp_socket_close(id);
                esp_reply(false);
            }
            if (result != 0) esp.opening = -1;
        }
        if (esp.sending >= 0 && esp.collected == esp.wanted) esp_flush_send();
    }
    for (int id = 0; id < ESP_LINKS; ++id)
        if (esp.connected[id]) esp_receive(id);
}

static bool esp_tx_ready(void)
{
    return esp_room() >= ESP_REPLY_SPACE && esp.opening < 0 &&
           (esp.sending < 0 || esp.collected < esp.wanted);
}

static unsigned int esp_at_status(void)
{
    esp_cycle();
    return (esp.count ? 1u : 0u) | (esp_tx_ready() ? 2u : 0u);
}

static unsigned int esp_at_read(void)
{
    esp_cycle();
    if (!esp.count) return 0;
    unsigned char byte = esp.rx[esp.head];
    esp.head = (esp.head + 1) % sizeof esp.rx;
    --esp.count;
    return byte;
}

static void esp_at_write(unsigned char byte)
{
    /* A prior status read can report ready. Do not poll sockets again
       before committing that byte, lest a new packet fill the RX queue. */
    if (!esp_tx_ready()) return;
    if (esp.sending >= 0) {
        esp.payload[esp.collected++] = byte;
    } else if (byte == '\n') {
        if (esp.discard) {
            esp_reply(false);
            esp.discard = false;
            esp.length = 0;
        } else {
            if (esp.length && esp.line[esp.length - 1] == '\r') --esp.length;
            esp_execute();
        }
    } else if (!esp.discard) {
        if ((byte < ' ' && byte != '\r') || esp.length == sizeof esp.line - 1)
            esp.discard = true;
        else
            esp.line[esp.length++] = byte;
    }
}

//-------------------------------------------------------------------
int input_char(int c, EvtType typ, int X, int Y)
{
    size_t next = input_write_ptr + 1;

    reg_event = 1;
    if (next == THOR_MAXINPUT) next = 0;
    if (next == input_read_ptr) return c; // queue full
    input_buffer[input_write_ptr] = c;
    input_event[input_write_ptr].typ = typ;
    input_event[input_write_ptr].X = X;
    input_event[input_write_ptr].Y = Y;
    input_write_ptr = next;
    return c;
}
//-------------------------------------------------------------------
void fb_open(int width, int height, char *data)
{
#if HAVE_X11
    imgWidth = width; imgHeight = height;

	xDisplay = XOpenDisplay(NULL);
	xScreenNum = DefaultScreen(xDisplay);
	Window root = RootWindow(xDisplay,xScreenNum);
	Visual *visual = DefaultVisual(xDisplay,xScreenNum);

	xImg = XCreateImage(xDisplay,visual,DefaultDepth(xDisplay,xScreenNum),ZPixmap,0,data,imgWidth,imgHeight,32,0);
    for (int x = 0; x < imgWidth; x++)
        for (int y = 0; y < imgHeight; y++) {
            unsigned  z = x + y;
            XPutPixel(xImg, x, y, (z << 16) + (x << 8)+y);
        }

	xWin = XCreateSimpleWindow(xDisplay,root,50,50,imgWidth,imgHeight,1,0,0);
	XSelectInput(xDisplay,xWin,ExposureMask|StructureNotifyMask|KeyPressMask|ButtonPressMask);
	XMapWindow(xDisplay,xWin);
#endif
}
//-------------------------------------------------------------------
void fb_flush()
{
#if HAVE_X11
    XFlush(xDisplay);
#endif
}
//-------------------------------------------------------------------
int fb_qevent()
{
#if HAVE_X11
    XEvent event;

    fb_flush();
    while (XCheckMaskEvent(xDisplay,-1,&event)) {
	    if (event.type == Expose ||
            event.type == MappingNotify ||
            event.type == KeyPress ||
            event.type == ButtonPress)
        {
            XPutBackEvent(xDisplay,&event);
            return 1;
        }
    }
#endif
    return 0;
}
//-------------------------------------------------------------------
int fb_event()
{
#if HAVE_X11
	XEvent event;

    fb_flush();
    while (1) {
        XNextEvent(xDisplay,&event);
	    if(event.type == Expose)
	    {
	        XPutImage(xDisplay,xWin,DefaultGC(xDisplay,xScreenNum),xImg,0,0,0,0,imgWidth,imgHeight);
            return 0;
	    }
        else if (event.type == MappingNotify) {
            XRefreshKeyboardMapping(&event.xmapping);
            return 0;
        }
        else if (event.type == KeyPress) {
            // KeySym sym = XLookupKeysym(&event.xkey,0);
            KeySym ksym;
            char buf[8];
            if (XLookupString(&event.xkey, buf, 1, &ksym, NULL))
                return input_char(buf[0], EvtKey, event.xkey.x, event.xkey.y);
            return 0;
        } else if (event.type == ButtonPress) {
            return input_char(128 + event.xbutton.button, EvtButton, event.xkey.x, event.xkey.y);
        }
    }
#endif
    return 0;
}
//-------------------------------------------------------------------
void fb_close()
{
#if HAVE_X11
    XDestroyWindow(xDisplay, xWin);
    XCloseDisplay(xDisplay);
#endif
}
//-------------------------------------------------------------------
void init_syms()
{
    memset(symaddr, 0, sizeof(symaddr));
    memset(symbols, 0, sizeof(symbols));
}
//-------------------------------------------------------------------
void add_sym(unsigned int addr, char *sym)
{
    unsigned int h;
    int i;

    h = addr & (MAX_SYM-1);
    for (i = 0; i < MAX_SYM; i++) {
        if (symbols[h] == NULL) {
            symaddr[h] = addr;
            symbols[h] = strdup(sym);
            return;
        }
        if (++h == MAX_SYM)
            h = 0;
    }
}
//-------------------------------------------------------------------
char *get_sym(unsigned int addr)
{
    unsigned int h;
    int i;

    h = addr & (MAX_SYM-1);
    for (i = 0; i < MAX_SYM; i++) {
        if (symbols[h] == NULL)
            return NULL;
        else if (symaddr[h] == addr)
            return symbols[h];
        if (++h == MAX_SYM)
            h = 0;
    }
    return NULL;
}
//-------------------------------------------------------------------
void load_syms(const char *name)
{
    FILE *fin;
    char buf[1024], *p, where[32], sym[128];
    unsigned int adr;
    int i;

    fin = fopen(name, "r");
    if (!fin) {
        fprintf(stderr,"Cannot open %s!\n",name);
        exit(1);
    }
    p = fgets(buf, sizeof(buf), fin);
    while (p) {
        while (*p && isspace(*p)) p++;
        i = 0;
        while (*p && !isspace(*p))
            sym[i++] = *p++;
        sym[i] = 0;
        while (*p && isspace(*p)) p++;
        i = 0;
        while (*p && !isspace(*p))
            where[i++] = *p++;
        where[i] = 0;

        if (*where && *sym) {
            adr = strtol(where, (char**)NULL, 16); 
            //fprintf(stderr,"adding %08x %s\n", adr, sym);
            add_sym(adr, sym);
        }
        p = fgets(buf, sizeof(buf), fin);
    }
    fclose(fin);
}
//-------------------------------------------------------------------
void dump_registers(void) {
    int i;
    static const char *flags="NZCVQ000000000000000000000000000";
    unsigned int mask;
    const char *p;

    fprintf(stderr, "\nRegisters:\n");
    fprintf(stderr, "cpsr: ");
    mask = 0x80000000;
    for (p=&flags[0];*p;p++) {
        if ('0' == *p) fprintf(stderr,"%c",(cpsr&mask)?'1':'0');
        else fprintf(stderr,"%c",(cpsr&mask)?*p:'-');
        mask >>= 1;
    }
    fprintf(stderr,"\n");
    for (i = 0; i < 16; i++) {
        switch (i) {
            case 13: fprintf(stderr, "  sp: "); break;
            case 14: fprintf(stderr, "  lr: "); break;
            case 15: fprintf(stderr, "  pc: "); break;
            default: fprintf(stderr, " %sr%d: ", i < 10 ? " " : "", i); break;
        }
        fprintf(stderr, "%08x   ", i == 13 && using_psp() ? psp : reg_norm[i]);
        if (0 == ((i+1) % 4)) fprintf(stderr,"\n");
    }
    fprintf(stderr,"\n");
}       
//-------------------------------------------------------------------
// The folded ROM halfword index is shared with the previous decode cache.
// ROM is immutable during execution. RAM and tracing use an uncached entry.
enum { CACHE_BITS = 10, CACHE_SIZE = 1 << CACHE_BITS };
enum { CACHE_VALID = 0x4000, CACHE_WIDE = 0x8000 };
enum { NO_TRACE_SUFFIX = 2 };

typedef struct DecodedInstruction DecodedInstruction;
typedef int (*InstructionExecutor)(unsigned int pc, const DecodedInstruction *decoded);

struct DecodedInstruction {
    InstructionExecutor execute;
    uint32_t operand; // Immediate, register list, or third register
    uint16_t tag;     // ROM tag, valid bit, and wide-instruction bit
    uint8_t reg1, reg2;
};

_Static_assert((ROMADDMASK >> (1 + CACHE_BITS)) < CACHE_VALID,
               "ROM tag must fit below the cache flags");
static DecodedInstruction cache[CACHE_SIZE];

#if CACHE_STATS
static unsigned long cache_hits, cache_misses, uncached_instructions;
#endif

void dump_counters ( void )
{
#if CACHE_STATS
    fprintf(stderr, "cache hits=%lu misses=%lu uncached=%lu bytes=%zu\n",
            cache_hits, cache_misses, uncached_instructions, sizeof(cache));
#endif
    printf("\n\n");
    printf("clocks       %lu\n",clks);
    printf("instructions %lu (CPI %.2f)\n",instructions,(clks + 0.0)/instructions);
    printf("fetches      %lu\n",fetches);
    printf("reads        %lu\n",reads);
    printf("writes       %lu\n",writes);
    printf("memcycles    %lu\n",fetches+reads+writes);
    printf("systick_ints %lu\n",systick_ints);
}
//-------------------------------------------------------------------
unsigned int fetch16 ( unsigned int addr )
{
    unsigned int data;

    fetches++;

if(DBUGFETCH) fprintf(stderr,"fetch16(0x%08X)=",addr);
if(DBUG) fprintf(stderr,"fetch16(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case ROM_START: //ROM
            addr&=ROMADDMASK;

//if(addr<0x50)
//{
//    fprintf(stderr,"fetch16(0x%08X), abort\n",addr);
//    exit(1);
//}

            addr>>=1;
            data=rom[addr];
if(DBUGFETCH) fprintf(stderr,"0x%04X\n",data);
if(DBUG) fprintf(stderr,"0x%04X\n",data);
            return(data);
        case RAM_START: //RAM
            addr&=RAMADDMASK;
            addr>>=1;
            data=ram[addr];
if(DBUGFETCH) fprintf(stderr,"0x%04X\n",data);
if(DBUG) fprintf(stderr,"0x%04X\n",data);
            return(data);
    }
    fprintf(stderr,"\nfetch16(0x%08X), abort pc = 0x%04X\n",addr,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
unsigned int fetch32 ( unsigned int addr )
{
    unsigned int data;

if(DBUGFETCH) fprintf(stderr,"fetch32(0x%08X)=",addr);
if(DBUG) fprintf(stderr,"fetch32(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case ROM_START: //ROM
            if(addr<0x50)
            {
                data=read32(addr);
if(DBUGFETCH) fprintf(stderr,"0x%08X\n",data);
if(DBUG) fprintf(stderr,"0x%08X\n",data);
                if(addr==0x00000000) return(data);
                if(addr==0x00000004) return(data);
                if(addr==4*EXCEPTION_HARDFAULT) return(data);
                if(addr==4*EXCEPTION_SVC) return(data);
                if(addr==4*EXCEPTION_SYSTICK) return(data);
                fprintf(stderr,"\nfetch32(0x%08X), abort pc = 0x%04X\n",addr,read_register(15));
                exit(1);
            }
        case RAM_START: //RAM
            //data=fetch16(addr+0);
            //data|=((unsigned int)fetch16(addr+2))<<16;
            data=read32(addr);
if(DBUGFETCH) fprintf(stderr,"0x%08X\n",data);
if(DBUG) fprintf(stderr,"0x%08X\n",data);
            return(data);
    }
    fprintf(stderr,"\nfetch32(0x%08X), abort pc 0x%04X\n",addr,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
void write16 ( unsigned int addr, unsigned int data )
{

    writes++;


if(DBUG) fprintf(stderr,"write16(0x%08X,0x%04X)\n",addr,data);
    switch(addr&0xF0000000)
    {
        case RAM_START: //RAM
if(DBUGRAM) fprintf(stderr,"write16(0x%08X,0x%04X)\n",addr,data);
            addr&=RAMADDMASK;
            addr>>=1;
            ram[addr]=data&0xFFFF;
            return;
    }
    fprintf(stderr,"\nwrite16(0x%08X,0x%04X), abort pc 0x%04X\n",addr,data,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
void write32 ( unsigned int addr, unsigned int data )
{
if(DBUG) fprintf(stderr,"write32(0x%08X,0x%08X)\n",addr,data);
    switch(addr&0xF0000000)
    {
        case 0xF0000000: //halt
            dump_counters();
            exit(0);
        case PERIPH_START: //periph
            if (0xE2000000 <= addr && addr < 0xE2F00000) /* frame buffer */
            {
                frame_buffer[(addr - 0xE2000000) >> 2] = data;
            }
            if (0xE3004000 <= addr && addr < 0xE3004200) /* disk buffer */
            {
                disk_buffer[(addr - 0xE3004000) >> 2] = data;
            }
            switch(addr)
            {
                case PERIPH_START + 0:
if(DISS)
{
                    fprintf(stderr,"uart: [");
                    write(error_fd, &data, 1);
                    fprintf(stderr,"]\n");
}
                    write(write_fd, &data, 1);
                    break;

                case ESP_AT_TX:
                    esp_at_write((unsigned char)data);
                    break;
                case 0xE2F00000: /* frame buffer port */
                {
                    fb_port = data;
                    break;
                }
                case 0xE3000000: /* disk offset low */
                {
                    disk_offset_low = data;
                    break;
                }
                case 0xE3000008: /* disk offset high */
                {
                    disk_offset_high = data;
                    break;
                }
                case 0xE3000010: /* select IDE ID */
                {
                    disk_ide_id = data;
                    break;
                }
                case 0xE3000020: /* start disk operation */
                {
                    handle_disk (data);
                    break;
                }
                case SHPR2:
                    shpr2 = data & 0xC0000000u;
                    break;
                case SHPR3:
                    shpr3 = data & 0xC0C00000u;
                    break;
                case 0xE000E010:
                {
                    unsigned int old;

                    old=systick_ctrl;
                    systick_ctrl = data&SYSTICK_MASK;
                    if(((old&SYSTICK_ENABLE)==0)&&(systick_ctrl&SYSTICK_ENABLE))
                    {
                        //timer started, load count
                        systick_count=systick_reload;
                    }
                    break;
                }
                case 0xE000E014:
                {
                    systick_reload=data&0x00FFFFFF;
                    break;
                }
                case 0xE000E018:
                {
                    systick_count=data&0x00FFFFFF;
                    systick_ctrl &= (~SYSTICK_COUNTFLAG);
                    break;
                }
                case 0xE000E01C:
                {
                    systick_calibrate=data&0x00FFFFFF;
                    break;
                }
            }
            return;
        case 0xD0000000: //debug
            switch(addr&0xFF)
            {
                case 0x00:
                {
                    fprintf(stderr,"[0x%08X][0x%08X] 0x%08X\n",read_register(14),addr,data);
                    return;
                }
                case 0x10:
                {
                    printf("0x%08X ",data);
                    return;
                }
                case 0x20:
                {
                    printf("0x%08X\n",data);
                    return;
                }
            }
        /* case ROM_START: disable ROM write */
        case RAM_START: //RAM
if(DBUGRAMW) fprintf(stderr,"write32(0x%08X,0x%08X)\n",addr,data);
            write16(addr+0,(data>> 0)&0xFFFF);
            write16(addr+2,(data>>16)&0xFFFF);
            return;
    }
    fprintf(stderr,"\nwrite32(0x%08X,0x%08X), abort pc 0x%04X\n",addr,data,read_register(15));
    exit(1);
}
//-----------------------------------------------------------------
unsigned int read16 ( unsigned int addr )
{
    unsigned int data;

    reads++;

if(DBUG) fprintf(stderr,"read16(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case ROM_START: //ROM
            addr&=ROMADDMASK;
            addr>>=1;
            data=rom[addr];
if(DBUG) fprintf(stderr,"0x%04X\n",data);
            return(data);
        case RAM_START: //RAM
if(DBUGRAM) fprintf(stderr,"read16(0x%08X)=",addr);
            addr&=RAMADDMASK;
            addr>>=1;
            data=ram[addr];
if(DBUG) fprintf(stderr,"0x%04X\n",data);
if(DBUGRAM) fprintf(stderr,"0x%04X\n",data);
            return(data);
    }
    fprintf(stderr,"\nread16(0x%08X), abort pc 0x%04X\n",addr,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
unsigned int read32 ( unsigned int addr )
{
    unsigned int data = 0;

if(DBUG) fprintf(stderr,"read32(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case ROM_START: //ROM
        case RAM_START: //RAM
if(DBUGRAMW) fprintf(stderr,"read32(0x%08X)=",addr);
            data =read16(addr+0);
            data|=((unsigned int)read16(addr+2))<<16;
if(DBUG) fprintf(stderr,"0x%08X\n",data);
if(DBUGRAMW) fprintf(stderr,"0x%08X\n",data);
            return(data);
        case PERIPH_START:
        {
            if (0xE2000000 <= addr && addr < 0xE2F00000) /* frame buffer */
            {
                return frame_buffer[(addr - 0xE2000000) >> 2];
            }
            if (0xE3004000 <= addr && addr < 0xE3004200) /* disk buffer */
            {
                return disk_buffer[(addr - 0xE3004000) >> 2];
            }
            switch(addr)
            {
                case PERIPH_START + 4:  /* read next char */
                {
if (DBUGUART) printf("uart: [%lu %lu ", input_read_ptr, input_write_ptr);
                    if (input_read_ptr != input_write_ptr) {
                        data = input_buffer[input_read_ptr++];
                        if (input_read_ptr == THOR_MAXINPUT) input_read_ptr = 0;
                    }
if (DBUGUART) printf("%c]\n", data);
if (DBUG) fprintf(stderr, "%08x\n", data);
                    return (data);
                }
                case PERIPH_START + 8:  /* -1 if char available */
                {
                    if (input_read_ptr != input_write_ptr) {
                        data = -1;
                    } else {
                        data = 0;
                    }
if (DBUG) fprintf(stderr, "%08x\n", data);
                    return (data);
                }
                case PERIPH_START + 12: /* read a single char */
                {
                    read(read_fd, &data, 1);
if (DBUG) fprintf(stderr, "%08x\n", data);
                    return (data);
                }
                case PERIPH_START + 16: /* event type */
                {
                    if (input_read_ptr != input_write_ptr)
                        data = input_event[input_read_ptr].typ;
                    return (data);
                }
                case PERIPH_START + 20: /* event X coord. */
                {
                    if (input_read_ptr != input_write_ptr)
                        data = input_event[input_read_ptr].X;
                    return (data);
                }
                case PERIPH_START + 24: /* event Y coord. */
                {
                    if (input_read_ptr != input_write_ptr)
                        data = input_event[input_read_ptr].Y;
                    return (data);
                }
                case ESP_AT_RX: return esp_at_read();
                case ESP_AT_STATUS: return esp_at_status();
                case HOST_SECONDS: return (unsigned int)time(NULL);
                case 0xE2F00000: /* frame buffer port */
                {
                    return fb_port;
                }
                case 0xE2F00010: /* frame buffer data */
                {
                    switch (fb_port) {
                    case 1: data = fb_width; break;
                    case 2: data = fb_height; break;
                    }
                    return(data);
                }
                case 0xE3000030: /* disk status */
                {
                    data = disk_status;
                    return(data);
                }
                case 0xE000E010:
                {
                    data = systick_ctrl;
                    systick_ctrl&=(~SYSTICK_COUNTFLAG);
                    return(data);
                }
                case 0xE000E014:
                {
                    data=systick_reload;
                    return(data);
                }
                case 0xE000E018:
                {
                    data=systick_count;
                    return(data);
                }
                case 0xE000E01C:
                {
                    data=systick_calibrate;
                    return(data);
                }
                case SHPR2: return shpr2;
                case SHPR3: return shpr3;
                case 0xE000ED00:
                {
                    data=cpuid;
if (DBUG) fprintf(stderr, "%08x\n", data);
                    return(data);
                }
            }
        }
    }
    fprintf(stderr,"\nread32(0x%08X), abort pc 0x%04X\n",addr,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
unsigned int read_register ( unsigned int reg )
{
    unsigned int data;

    reg&=0xF;
if(DBUG) fprintf(stderr,"read_register(%u)=",reg);
if(DBUGREG) fprintf(stderr,"read_register(%u)=",reg);
    data = reg == 13 && using_psp() ? psp : reg_norm[reg];
    if(reg==15)
    {
        if(data&1)
        {
            fprintf(stderr,"pc has lsbit set 0x%08X\n",data);
        }
        data&=~1;
    }
if(DBUG) fprintf(stderr,"0x%08X\n",data);
if(DBUGREG) fprintf(stderr,"0x%08X\n",data);
    return(data);
}
//-------------------------------------------------------------------
void write_register ( unsigned int reg, unsigned int data )
{
    reg&=0xF;
if(DBUG) fprintf(stderr,"write_register(%u,0x%08X)\n",reg,data);
if(DBUGREG) fprintf(stderr,"write_register(%u,0x%08X)\n",reg,data);
    if(reg==15) data&=~1;
    if (reg == 13 && using_psp()) psp = data;
    else reg_norm[reg] = data;

if(output_vcd)
{
    unsigned int vv;
    fprintf(fpvcd,"b");
    for(vv=0x80000000;vv;vv>>=1)
    {
        if(vv&data) fprintf(fpvcd,"1"); else fprintf(fpvcd,"0");
    }
    fprintf(fpvcd," r%u\n",reg);
}

}
//-------------------------------------------------------------------
void do_zflag ( unsigned int x )
{
    if(x==0) cpsr|=CPSR_Z; else cpsr&=~CPSR_Z;
}
//-------------------------------------------------------------------
void do_nflag ( unsigned int x )
{
    if(x&0x80000000) cpsr|=CPSR_N; else cpsr&=~CPSR_N;
}
//-------------------------------------------------------------------
void do_cflag ( unsigned int a, unsigned int b, unsigned int c )
{
    unsigned int rc;

    cpsr&=~CPSR_C;
    rc=(a&0x7FFFFFFF)+(b&0x7FFFFFFF)+c; //carry in
    rc = (rc>>31)+(a>>31)+(b>>31);  //carry out
    if(rc&2) cpsr|=CPSR_C;
}
//-------------------------------------------------------------------
void do_vflag ( unsigned int a, unsigned int b, unsigned int c )
{
    unsigned int rc;
    unsigned int rd;

    cpsr&=~CPSR_V;
    rc=(a&0x7FFFFFFF)+(b&0x7FFFFFFF)+c; //carry in
    rc>>=31; //carry in in lsbit
    rd=(rc&1)+((a>>31)&1)+((b>>31)&1); //carry out
    rd>>=1; //carry out in lsbit
    rc=(rc^rd)&1; //if carry in != carry out then signed overflow
    if(rc) cpsr|=CPSR_V;
}
//-------------------------------------------------------------------
void do_cflag_bit ( unsigned int x )
{
   if(x) cpsr|=CPSR_C; else cpsr&=~CPSR_C;
}
//-------------------------------------------------------------------
void do_vflag_bit ( unsigned int x )
{
   if(x) cpsr|=CPSR_V; else cpsr&=~CPSR_V;
}
//-------------------------------------------------------------------
void wait_for_input(void)
{
    fd_set s_rd;

    if (input_read_ptr == input_write_ptr) {
        FD_ZERO(&s_rd);
        FD_SET(read_fd, &s_rd);
        select(read_fd + 1, &s_rd, NULL, NULL, NULL);
    }
}
//-------------------------------------------------------------------
void handle_disk(unsigned int wr)
{
    ssize_t rc;
    int fd;
    off_t off;

    disk_status = 0;

    if (disk_ide_id >= disk_count)
        return;
    fd = disk_fd[disk_ide_id];

    uint64_t offset = ((uint64_t)disk_offset_high << 32) | disk_offset_low;
    if (disk_size[disk_ide_id] < 0 || offset > (uint64_t)disk_size[disk_ide_id])
        return;
    off = lseek(fd, (off_t)offset, SEEK_SET);
    if (-1 == off)
        return;

    if (wr)
        rc = write(fd, disk_buffer, sizeof(disk_buffer));
    else
        rc = read(fd, disk_buffer, sizeof(disk_buffer));
    disk_status = (rc < 0 || rc != sizeof(disk_buffer)) ? 0 : 1;
}
//-------------------------------------------------------------------
int handle_bkpt(unsigned int bp, unsigned int arg)
{
	int r = 1;
	FILE *f;
	int s, e, n;
	unsigned int sp;

	sp = read_register(13);
	switch (arg) {
		case 0x18:
			fprintf(stderr, "Exiting.\n");
			break;
		case 0x80:
			s = read32(sp + 8);
			e = read32(sp + 4);
			fprintf(stderr, "Dumping from %08x to %08x into %s...\n",
					s, e, output_file_name);
			f = fopen(output_file_name, "wb");
			while (s != e) {
				n = read32(s);
				fwrite(&n, 2, 1, f);
				s += 2;
			}
			fclose(f);
			write_register(13, read_register(13) + 8);
			r = 0;
			break;
		case 0x81:
			dump_registers();
			r = 0;
			break;
		case 0x82:
			wait_for_input();
			r = 0;
			break;
		default:
			fprintf(stderr, "bkpt 0x%02X %08x\n", bp, arg);
			break;
	}
	write_register(13, read_register(13) + 4);
	return r;
}


//-------------------------------------------------------------------
//-------------------------------------------------------------------
//-------------------------------------------------------------------
static void advance_systick(unsigned long ticks)
{
    unsigned int period = systick_reload + 1;

    if (!(systick_ctrl & SYSTICK_ENABLE) || ticks == 0)
        return;
    if (ticks < systick_count) {
        systick_count -= ticks;
        return;
    }
    if (systick_count != 0 || (systick_reload != 0 && ticks >= period)) {
        systick_ctrl |= SYSTICK_COUNTFLAG;
        if (systick_ctrl & SYSTICK_TICKINT)
            systick_pending = 1;
    }
    systick_count = (period - (ticks - systick_count) % period) % period;
}

static unsigned int read_special(unsigned int sysm)
{
    const unsigned int flags = cpsr & 0xF0000000u;
    switch (sysm) {
    case 0: case 2: return flags;                    // APSR, EAPSR
    case 1: case 3: return flags | active_exception; // IAPSR, xPSR
    case 5: case 7: return active_exception;         // IPSR, IEPSR
    case 8: return reg_norm[13];                     // MSP
    case 9: return psp;
    case 16: return primask;
    case 20: return active_exception ? 0 : control; // Cortex-M0 implements SPSEL only
    default: return 0;                              // EPSR and reserved encodings
    }
}

static void write_special(unsigned int sysm, unsigned int value)
{
    switch (sysm) {
    case 0: case 1: case 2: case 3:
        cpsr = (cpsr & 0x0FFFFFFFu) | (value & 0xF0000000u);
        break;
    case 8: reg_norm[13] = value & ~3u; break;
    case 9: psp = value & ~3u; break;
    case 16: primask = value & 1u; break;
    case 20:
        if (!active_exception) control = value & CONTROL_SPSEL;
        break;
    }
}

static int exception_priority(unsigned int exception)
{
    switch (exception) {
    case EXCEPTION_HARDFAULT: return -1;
    case EXCEPTION_SVC: return shpr2 >> 30;
    case EXCEPTION_SYSTICK: return shpr3 >> 30;
    default: return 4; // Thread mode
    }
}

static int can_take_exception(unsigned int exception)
{
    return !primask && exception_priority(exception) < exception_priority(active_exception);
}

static int valid_vector(unsigned int vector)
{
    unsigned int region = vector & 0xF0000000u;
    return (vector & 1) && (region == ROM_START || region == RAM_START);
}

static unsigned int enter_exception(unsigned int number, unsigned int return_address,
                                    unsigned int vector)
{
    unsigned int sp = read_register(13);
    unsigned int xpsr = cpsr | XPSR_THUMB | active_exception;

    if (sp & 4)
        xpsr |= XPSR_STACK_ALIGN;
    sp = (sp & ~7u) - 32;
    write32(sp + 0, read_register(0));
    write32(sp + 4, read_register(1));
    write32(sp + 8, read_register(2));
    write32(sp + 12, read_register(3));
    write32(sp + 16, read_register(12));
    write32(sp + 20, read_register(14));
    write32(sp + 24, return_address);
    write32(sp + 28, xpsr);
    clks += 16;
    write_register(13, sp);
    write_register(14, active_exception ? EXC_RETURN_HANDLER :
                   using_psp() ? EXC_RETURN_THREAD_PSP : EXC_RETURN_THREAD_MSP);
    active_exception = number;
    reg_event = 1;
    return (vector & ~1u) + 2; // internal PC points two bytes past the next fetch
}

static unsigned int return_from_exception(unsigned int exc_return)
{
    unsigned int *stack = exc_return == EXC_RETURN_THREAD_PSP ? &psp : &reg_norm[13];
    unsigned int sp = *stack;
    unsigned int pc = read32(sp + 24);
    unsigned int xpsr = read32(sp + 28);

    write_register(0, read32(sp + 0));
    write_register(1, read32(sp + 4));
    write_register(2, read32(sp + 8));
    write_register(3, read32(sp + 12));
    write_register(12, read32(sp + 16));
    write_register(14, read32(sp + 20));
    *stack = sp + 32 + ((xpsr & XPSR_STACK_ALIGN) ? 4 : 0);
    if (exc_return != EXC_RETURN_HANDLER)
        control = exc_return == EXC_RETURN_THREAD_PSP ? CONTROL_SPSEL : 0;
    clks += 16;
    cpsr = xpsr & XPSR_FLAGS;
    active_exception = xpsr & 0x1FF;
    reg_event = 1;
    return (pc & ~1u) + 2;
}

static int hardfault(const char *reason, unsigned int address)
{
    unsigned int vector;

    if (active_exception == EXCEPTION_HARDFAULT) {
        fprintf(stderr, "HardFault lockup at 0x%08X\n", address);
        return 1;
    }
    vector = fetch32(entry + 4 * EXCEPTION_HARDFAULT);
    if (!valid_vector(vector)) {
        fprintf(stderr, "%s at 0x%08X: invalid HardFault vector 0x%08X\n", reason, address, vector);
        return 1;
    }
    write_register(15, enter_exception(EXCEPTION_HARDFAULT, address, vector));
    return 0;
}

// Some instructions supply their own trace ending, even when they continue.
static int without_trace_suffix(int result)
{
    return DISS && result == 0 ? NO_TRACE_SUFFIX : result;
}

static int execute_mrs(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rd = decoded->reg1;
    unsigned int sysm = decoded->operand;

    if (DISS) fprintf(stderr, "mrs r%u, sysm=%u", rd, sysm);
    write_register(rd, read_special(sysm));
    write_register(15, pc + 2);
    clks += 2;
    return 0;
}

static int decode_mrs(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr >> 24) & 15;
    decoded->operand = (instr >> 16) & 255;
    decoded->execute = execute_mrs;
    return execute_mrs(pc, decoded);
}

static int execute_msr(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rn = decoded->reg1;
    unsigned int sysm = decoded->operand;

    if (DISS) fprintf(stderr, "msr sysm=%u, r%u", sysm, rn);
    write_special(sysm, read_register(rn));
    write_register(15, pc + 2);
    clks += 2;
    return 0;
}

static int decode_msr(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr & 15;
    decoded->operand = (instr >> 16) & 255;
    decoded->execute = execute_msr;
    return execute_msr(pc, decoded);
}

static int execute_dsb(unsigned int pc, const DecodedInstruction *decoded)
{
    // Memory accesses complete synchronously and there is no prefetch.
    if (DISS) fprintf(stderr, "dsb");
    write_register(15, pc + 2);
    clks++;
    return 0;
}

static int decode_dsb(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->execute = execute_dsb;
    return execute_dsb(pc, decoded);
}

static int execute_dmb(unsigned int pc, const DecodedInstruction *decoded)
{
    // Memory accesses complete synchronously and there is no prefetch.
    if (DISS) fprintf(stderr, "dmb");
    write_register(15, pc + 2);
    clks++;
    return 0;
}

static int decode_dmb(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->execute = execute_dmb;
    return execute_dmb(pc, decoded);
}

static int execute_isb(unsigned int pc, const DecodedInstruction *decoded)
{
    // Memory accesses complete synchronously and there is no prefetch.
    if (DISS) fprintf(stderr, "isb");
    write_register(15, pc + 2);
    clks++;
    return 0;
}

static int decode_isb(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->execute = execute_isb;
    return execute_isb(pc, decoded);
}

static int execute_udf_w(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    if (DISS) fprintf(stderr, "udf.w #%u\n", rb);
    return without_trace_suffix(hardfault("UDF", pc - 4));
}

static int decode_udf_w(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 15) << 12) | ((instr >> 16) & 0xFFF);
    decoded->execute = execute_udf_w;
    return execute_udf_w(pc, decoded);
}

static int execute_bl(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int offset = decoded->operand;

    if (DISS) fprintf(stderr, "bl    0x%08X", pc + offset);
    write_register(14, pc | 1);
    write_register(15, pc + offset + 2);
    clks += 2;
    return 0;
}

static int decode_bl(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    unsigned int second = instr >> 16;
    unsigned int sign = (instr >> 10) & 1;
    unsigned int i1 = ((second >> 13) & 1) ^ sign ^ 1;
    unsigned int i2 = ((second >> 11) & 1) ^ sign ^ 1;
    unsigned int offset = (i1 << 23) | (i2 << 22) |
                          ((instr & 0x3FF) << 12) | ((second & 0x7FF) << 1);

    if (sign) offset |= 0xFF000000u;
    decoded->operand = offset;
    decoded->execute = execute_bl;
    return execute_bl(pc, decoded);
}

static int execute_lsl_imm(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->reg2;

    if (DISS) fprintf(stderr,"lsls  r%u,r%u,#0x%X",rd,rm,rb);
    rc=read_register(rm);
    if(rb==0)
    {
        //if immed_5 == 0
        // Carry is unchanged.
        //result not shifted
    }
    else
    {
        //else immed_5 > 0
        do_cflag_bit(rc&(1<<(32-rb)));
        rc<<=rb;
    }
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_lsl_imm(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->reg2 = (instr>>3)&0x07;
    decoded->operand = (instr>>6)&0x1F;
    decoded->execute = execute_lsl_imm;
    return execute_lsl_imm(pc, decoded);
}

static int execute_lsr_imm(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->reg2;

    if (DISS) fprintf(stderr,"lsrs  r%u,r%u,#0x%X",rd,rm,rb);
    rc=read_register(rm);
    if(rb==0)
    {
        do_cflag_bit(rc&0x80000000);
        rc=0;
    }
    else
    {
        do_cflag_bit(rc&(1<<(rb-1)));
        rc>>=rb;
    }
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_lsr_imm(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->reg2 = (instr>>3)&0x07;
    decoded->operand = (instr>>6)&0x1F;
    decoded->execute = execute_lsr_imm;
    return execute_lsr_imm(pc, decoded);
}

static int execute_asr_imm(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->reg2;

    if (DISS) fprintf(stderr,"asrs  r%u,r%u,#0x%X",rd,rm,rb);
    rc=read_register(rm);
    if(rb==0)
    {
        if(rc&0x80000000)
        {
            do_cflag_bit(1);
            rc=MINUS_ONE;
        }
        else
        {
            do_cflag_bit(0);
            rc=0;
        }
    }
    else
    {
        do_cflag_bit(rc&(1<<(rb-1)));
        ra=rc&0x80000000;
        rc>>=rb;
        if(ra) //asr, sign is shifted in
        {
            rc|=(MINUS_ONE)<<(32-rb);
        }
    }
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_asr_imm(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->reg2 = (instr>>3)&0x07;
    decoded->operand = (instr>>6)&0x1F;
    decoded->execute = execute_asr_imm;
    return execute_asr_imm(pc, decoded);
}

static int execute_add_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"adds  r%u,r%u,r%u",rd,rn,rm);
    ra=read_register(rn);
    rb=read_register(rm);
    rc=ra+rb;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    do_cflag(ra,rb,0);
    do_vflag(ra,rb,0);
    return 0;
}

static int decode_add_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->reg2 = (instr>>3)&0x7;
    decoded->operand = (instr>>6)&0x7;
    decoded->execute = execute_add_reg;
    return execute_add_reg(pc, decoded);
}

static int execute_sub_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"subs  r%u,r%u,r%u",rd,rn,rm);
    ra=read_register(rn);
    rb=read_register(rm);
    rc=ra-rb;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    do_cflag(ra,~rb,1);
    do_vflag(ra,~rb,1);
    return 0;
}

static int decode_sub_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->reg2 = (instr>>3)&0x7;
    decoded->operand = (instr>>6)&0x7;
    decoded->execute = execute_sub_reg;
    return execute_sub_reg(pc, decoded);
}

static int execute_add_imm3(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;

    if (DISS) fprintf(stderr,"adds  r%u,r%u,#0x%X",rd,rn,rb);
    ra=read_register(rn);
    rc=ra+rb;

    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    do_cflag(ra,rb,0);
    do_vflag(ra,rb,0);
    return 0;
}

static int decode_add_imm3(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&7;
    decoded->reg2 = (instr>>3)&7;
    decoded->operand = (instr>>6)&7;
    decoded->execute = execute_add_imm3;
    return execute_add_imm3(pc, decoded);
}

static int execute_mov_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->operand;

    if (DISS) fprintf(stderr,"movs  r%u,r%u",rd,rn);
    rc=read_register(rn);

    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    do_cflag_bit(0);
    do_vflag_bit(0);
    return 0;
}

static int decode_mov_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&7;
    decoded->operand = (instr>>3)&7;
    decoded->execute = execute_mov_reg;
    return execute_mov_reg(pc, decoded);
}

static int execute_sub_imm3(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;

    if (DISS) fprintf(stderr,"subs  r%u,r%u,#0x%X",rd,rn,rb);
    ra=read_register(rn);
    rc=ra-rb;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    do_cflag(ra,~rb,1);
    do_vflag(ra,~rb,1);
    return 0;
}

static int decode_sub_imm3(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&7;
    decoded->reg2 = (instr>>3)&7;
    decoded->operand = (instr>>6)&7;
    decoded->execute = execute_sub_imm3;
    return execute_sub_imm3(pc, decoded);
}

static int execute_mov_imm(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;
    unsigned int rd = decoded->reg1;

    if (DISS) fprintf(stderr,"movs  r%u,#0x%02X",rd,rb);
    write_register(rd,rb);
    do_nflag(rb);
    do_zflag(rb);
    return 0;
}

static int decode_mov_imm(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr>>8)&0x07;
    decoded->operand = instr&0xFF;
    decoded->execute = execute_mov_imm;
    return execute_mov_imm(pc, decoded);
}

static int execute_cmp_imm(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rn = decoded->reg1;

    if (DISS) fprintf(stderr,"cmp   r%u,#0x%02X",rn,rb);
    ra=read_register(rn);
    rc=ra-rb;

    do_nflag(rc);
    do_zflag(rc);
    do_cflag(ra,~rb,1);
    do_vflag(ra,~rb,1);
    return 0;
}

static int decode_cmp_imm(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr>>8)&0x07;
    decoded->operand = instr&0xFF;
    decoded->execute = execute_cmp_imm;
    return execute_cmp_imm(pc, decoded);
}

static int execute_add_imm8(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;

    if (DISS) fprintf(stderr,"adds  r%u,#0x%02X",rd,rb);
    ra=read_register(rd);
    rc=ra+rb;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    do_cflag(ra,rb,0);
    do_vflag(ra,rb,0);
    return 0;
}

static int decode_add_imm8(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr>>8)&0x7;
    decoded->operand = instr&0xFF;
    decoded->execute = execute_add_imm8;
    return execute_add_imm8(pc, decoded);
}

static int execute_sub_imm8(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;

    if (DISS) fprintf(stderr,"subs  r%u,#0x%02X",rd,rb);
    ra=read_register(rd);
    rc=ra-rb;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    do_cflag(ra,~rb,1);
    do_vflag(ra,~rb,1);
    return 0;
}

static int decode_sub_imm8(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr>>8)&0x07;
    decoded->operand = instr&0xFF;
    decoded->execute = execute_sub_imm8;
    return execute_sub_imm8(pc, decoded);
}

static int execute_and(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"ands  r%u,r%u",rd,rm);
    ra=read_register(rd);
    rb=read_register(rm);
    rc=ra&rb;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_and(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_and;
    return execute_and(pc, decoded);
}

static int execute_eor(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"eors  r%u,r%u",rd,rm);
    ra=read_register(rd);
    rb=read_register(rm);
    rc=ra^rb;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_eor(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_eor;
    return execute_eor(pc, decoded);
}

static int execute_lsl_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rs = decoded->operand;

    if (DISS) fprintf(stderr,"lsls  r%u,r%u",rd,rs);
    rc=read_register(rd);
    rb=read_register(rs);
    rb&=0xFF;
    if(rb==0)
    {
    }
    else if(rb<32)
    {
        do_cflag_bit(rc&(1<<(32-rb)));
        rc<<=rb;
    }
    else if(rb==32)
    {
        do_cflag_bit(rc&1);
        rc=0;
    }
    else
    {
        do_cflag_bit(0);
        rc=0;
    }
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_lsl_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->operand = (instr>>3)&0x07;
    decoded->execute = execute_lsl_reg;
    return execute_lsl_reg(pc, decoded);
}

static int execute_lsr_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rs = decoded->operand;

    if (DISS) fprintf(stderr,"lsrs  r%u,r%u",rd,rs);
    rc=read_register(rd);
    rb=read_register(rs);
    rb&=0xFF;
    if(rb==0)
    {
    }
    else if(rb<32)
    {
        do_cflag_bit(rc&(1<<(rb-1)));
        rc>>=rb;
    }
    else if(rb==32)
    {
        do_cflag_bit(rc&0x80000000);
        rc=0;
    }
    else
    {
        do_cflag_bit(0);
        rc=0;
    }
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_lsr_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->operand = (instr>>3)&0x07;
    decoded->execute = execute_lsr_reg;
    return execute_lsr_reg(pc, decoded);
}

static int execute_asr_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rs = decoded->operand;

    if (DISS) fprintf(stderr,"asrs  r%u,r%u",rd,rs);
    rc=read_register(rd);
    rb=read_register(rs);
    rb&=0xFF;
    if(rb==0)
    {
    }
    else if(rb<32)
    {
        do_cflag_bit(rc&(1<<(rb-1)));
        ra=rc&0x80000000;
        rc>>=rb;
        if(ra) //asr, sign is shifted in
        {
            rc|=(MINUS_ONE)<<(32-rb);
        }
    }
    else
    {
        if(rc&0x80000000)
        {
            do_cflag_bit(1);
            rc=(MINUS_ONE);
        }
        else
        {
            do_cflag_bit(0);
            rc=0;
        }
    }
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_asr_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->operand = (instr>>3)&0x07;
    decoded->execute = execute_asr_reg;
    return execute_asr_reg(pc, decoded);
}

static int execute_adc(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"adc   r%u,r%u",rd,rm);
    ra=read_register(rd);
    rb=read_register(rm);
    rc=ra+rb;
    if(cpsr&CPSR_C) rc++;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    if(cpsr&CPSR_C) { do_cflag(ra,rb,1); do_vflag(ra,rb,1); }
    else            { do_cflag(ra,rb,0); do_vflag(ra,rb,0); }
    return 0;
}

static int decode_adc(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->operand = (instr>>3)&0x07;
    decoded->execute = execute_adc;
    return execute_adc(pc, decoded);
}

static int execute_sbc(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"sbc   r%u,r%u",rd,rm);
    ra=read_register(rd);
    rb=read_register(rm);
    rc=ra-rb;
    if(!(cpsr&CPSR_C)) rc--;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    if(cpsr&CPSR_C)
    {
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
    }
    else
    {
        do_cflag(ra,~rb,0);
        do_vflag(ra,~rb,0);
    }
    return 0;
}

static int decode_sbc(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_sbc;
    return execute_sbc(pc, decoded);
}

static int execute_ror(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rs = decoded->operand;

    if (DISS) fprintf(stderr,"rors  r%u,r%u",rd,rs);
    rc=read_register(rd);
    ra=read_register(rs);
    ra&=0xFF;
    if(ra==0)
    {
    }
    else
    {
        ra&=0x1F;
        if(ra==0)
        {
            do_cflag_bit(rc&0x80000000);
        }
        else
        {
            do_cflag_bit(rc&(1<<(ra-1)));
            rb=rc<<(32-ra);
            rc>>=ra;
            rc|=rb;
        }
    }
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_ror(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_ror;
    return execute_ror(pc, decoded);
}

static int execute_tst(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rn = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"tst   r%u,r%u",rn,rm);
    ra=read_register(rn);
    rb=read_register(rm);
    rc=ra&rb;
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_tst(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_tst;
    return execute_tst(pc, decoded);
}

static int execute_neg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"negs  r%u,r%u",rd,rm);
    ra=read_register(rm);
    rc=0-ra;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    do_cflag(0,~ra,1);
    do_vflag(0,~ra,1);
    return 0;
}

static int decode_neg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_neg;
    return execute_neg(pc, decoded);
}

static int execute_cmp_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rn = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"cmps  r%u,r%u",rn,rm);
    ra=read_register(rn);
    rb=read_register(rm);
    rc=ra-rb;

    do_nflag(rc);
    do_zflag(rc);
    do_cflag(ra,~rb,1);
    do_vflag(ra,~rb,1);
    return 0;
}

static int decode_cmp_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_cmp_reg;
    return execute_cmp_reg(pc, decoded);
}

static int execute_cmn(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rn = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"cmns  r%u,r%u",rn,rm);
    ra=read_register(rn);
    rb=read_register(rm);
    rc=ra+rb;
    do_nflag(rc);
    do_zflag(rc);
    do_cflag(ra,rb,0);
    do_vflag(ra,rb,0);
    return 0;
}

static int decode_cmn(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_cmn;
    return execute_cmn(pc, decoded);
}

static int execute_orr(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"orrs  r%u,r%u",rd,rm);
    ra=read_register(rd);
    rb=read_register(rm);
    rc=ra|rb;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_orr(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_orr;
    return execute_orr(pc, decoded);
}

static int execute_mul(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"muls  r%u,r%u",rd,rm);
    ra=read_register(rd);
    rb=read_register(rm);
    rc=ra*rb; clks += 31;
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_mul(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_mul;
    return execute_mul(pc, decoded);
}

static int execute_bic(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"bics  r%u,r%u",rd,rm);
    ra=read_register(rd);
    rb=read_register(rm);
    rc=ra&(~rb);
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_bic(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_bic;
    return execute_bic(pc, decoded);
}

static int execute_mvn(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"mvns  r%u,r%u",rd,rm);
    ra=read_register(rm);
    rc=(~ra);
    write_register(rd,rc);
    do_nflag(rc);
    do_zflag(rc);
    return 0;
}

static int decode_mvn(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_mvn;
    return execute_mvn(pc, decoded);
}

static int execute_add_high(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"add   r%u,r%u",rd,rm);
    ra=read_register(rd);
    rb=read_register(rm);
    rc=ra+rb;
    if(rd==15)
    {
        if((rc&1)==0)
        {
            //ATfE 22.1.0 generates code which triggers this
            if(DBUGPC)fprintf(stderr,"add pc,... produced an arm address 0x%08X 0x%08X\n",pc,rc);

        }
        rc&=~1; //write_register may do this as well
        rc+=2; //The program counter is special
        clks += 2;
    }

    write_register(rd,rc);
    return 0;
}

static int decode_add_high(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr&0x7) | ((instr>>4)&0x8);
    decoded->operand = (instr>>3)&0xF;
    decoded->execute = execute_add_high;
    return execute_add_high(pc, decoded);
}

static int execute_cmp_high(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rn = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"cmps  r%u,r%u",rn,rm);
    ra=read_register(rn);
    rb=read_register(rm);
    rc=ra-rb;
    do_nflag(rc);
    do_zflag(rc);
    do_cflag(ra,~rb,1);
    do_vflag(ra,~rb,1);
    return 0;
}

static int decode_cmp_high(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr&0x7) | ((instr>>4)&0x8);
    decoded->operand = (instr>>3)&0xF;
    decoded->execute = execute_cmp_high;
    return execute_cmp_high(pc, decoded);
}

static int execute_mov_high(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"mov   r%u,r%u",rd,rm);
    rc=read_register(rm);
    if(rd==15)
    {
        rc&=~1; //write_register may do this as well
        rc+=2; //The program counter is special
        clks++;
    }
    write_register(rd,rc);
    return 0;
}

static int decode_mov_high(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr&0x7) | ((instr>>4)&0x8);
    decoded->operand = (instr>>3)&0xF;
    decoded->execute = execute_mov_high;
    return execute_mov_high(pc, decoded);
}

static int execute_bx(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rc;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"bx    r%u",rm);
    rc=read_register(rm);
    rc+=2; clks++;

    if(rc&1)
    {
        rc&=~1;
        write_register(15,rc);
        return 0;
    }
    else
    {
        fprintf(stderr,"\ncannot branch to arm 0x%08X 0x%04X\n",pc,0x4700u | (rm << 3));
        return(1);
    }
}

static int decode_bx(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = (instr>>3)&0xF;
    decoded->execute = execute_bx;
    return execute_bx(pc, decoded);
}

static int execute_blx_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rc;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"blx   r%u",rm);
    rc=read_register(rm);

    rc+=2; clks++;
    if(rc&1)
    {
        write_register(14,(pc-2)|1);
        rc&=~1;
        write_register(15,rc);
        return 0;
    }
    else
    {
        fprintf(stderr,"\ncannot branch to arm 0x%08X 0x%04X\n",pc,0x4780u | (rm << 3));
        return(1);
    }
}

static int decode_blx_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = (instr>>3)&0xF;
    decoded->execute = execute_blx_reg;
    return execute_blx_reg(pc, decoded);
}

static int execute_ldr_literal(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;

    if (DISS) fprintf(stderr,"ldr   r%u,[pc+#0x%X] ",rd,rb);
    ra=read_register(15);
    ra&=~3;
    rb+=ra;
    if (DISS) fprintf(stderr,";@ 0x%X",rb);
    rc=read32(rb); clks++;
    write_register(rd,rc);
    return 0;
}

static int decode_ldr_literal(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr>>8)&0x07;
    decoded->operand = (instr&0xFF) << 2;
    decoded->execute = execute_ldr_literal;
    return execute_ldr_literal(pc, decoded);
}

static int execute_str_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"str   r%u,[r%u,r%u]",rd,rn,rm);
    rb=read_register(rn)+read_register(rm);
    rc=read_register(rd);
    write32(rb,rc); clks++;
    return 0;
}

static int decode_str_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->reg2 = (instr>>3)&0x7;
    decoded->operand = (instr>>6)&0x7;
    decoded->execute = execute_str_reg;
    return execute_str_reg(pc, decoded);
}

static int execute_strh_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"strh  r%u,[r%u,r%u]",rd,rn,rm);
    rb=read_register(rn)+read_register(rm);
    rc=read_register(rd);
    write16(rb,rc&0xFFFF); clks++;
    return 0;
}

static int decode_strh_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->reg2 = (instr>>3)&0x7;
    decoded->operand = (instr>>6)&0x7;
    decoded->execute = execute_strh_reg;
    return execute_strh_reg(pc, decoded);
}

static int execute_strb_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"strb  r%u,[r%u,r%u]",rd,rn,rm);
    rb=read_register(rn)+read_register(rm);
    rc=read_register(rd);
    ra=read16(rb&(~1)); clks++;
    if(rb&1)
    {
        ra&=0x00FF;
        ra|=rc<<8;
    }
    else
    {
        ra&=0xFF00;
        ra|=rc&0x00FF;
    }
    write16(rb&(~1),ra&0xFFFF); clks++;
    return 0;
}

static int decode_strb_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->reg2 = (instr>>3)&0x7;
    decoded->operand = (instr>>6)&0x7;
    decoded->execute = execute_strb_reg;
    return execute_strb_reg(pc, decoded);
}

static int execute_ldrsb(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"ldrsb r%u,[r%u,r%u]",rd,rn,rm);
    rb=read_register(rn)+read_register(rm);
    rc=read16(rb&(~1)); clks++;
    if(rb&1)
    {
        rc>>=8;
    }

    rc&=0xFF;
    if(rc&0x80) rc|=((MINUS_ONE)<<8);
    write_register(rd,rc);
    return 0;
}

static int decode_ldrsb(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->reg2 = (instr>>3)&0x7;
    decoded->operand = (instr>>6)&0x7;
    decoded->execute = execute_ldrsb;
    return execute_ldrsb(pc, decoded);
}

static int execute_ldr_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"ldr   r%u,[r%u,r%u]",rd,rn,rm);
    rb=read_register(rn)+read_register(rm);
    rc=read32(rb); clks++;
    write_register(rd,rc);
    return 0;
}

static int decode_ldr_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->reg2 = (instr>>3)&0x7;
    decoded->operand = (instr>>6)&0x7;
    decoded->execute = execute_ldr_reg;
    return execute_ldr_reg(pc, decoded);
}

static int execute_ldrh_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"ldrh  r%u,[r%u,r%u]",rd,rn,rm);
    rb=read_register(rn)+read_register(rm);
    rc=read16(rb); clks++;
    write_register(rd,rc&0xFFFF);
    return 0;
}

static int decode_ldrh_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->reg2 = (instr>>3)&0x7;
    decoded->operand = (instr>>6)&0x7;
    decoded->execute = execute_ldrh_reg;
    return execute_ldrh_reg(pc, decoded);
}

static int execute_ldrb_reg(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"ldrb  r%u,[r%u,r%u]",rd,rn,rm);
    rb=read_register(rn)+read_register(rm);
    rc=read16(rb&(~1)); clks++;
    if(rb&1)
    {
        rc>>=8;
    }

    write_register(rd,rc&0xFF);
    return 0;
}

static int decode_ldrb_reg(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->reg2 = (instr>>3)&0x7;
    decoded->operand = (instr>>6)&0x7;
    decoded->execute = execute_ldrb_reg;
    return execute_ldrb_reg(pc, decoded);
}

static int execute_ldrsh(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"ldrsh r%u,[r%u,r%u]",rd,rn,rm);
    rb=read_register(rn)+read_register(rm);
    rc=read16(rb); clks++;
    rc&=0xFFFF;
    if(rc&0x8000) rc|=((MINUS_ONE)<<16);
    write_register(rd,rc);
    return 0;
}

static int decode_ldrsh(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->reg2 = (instr>>3)&0x7;
    decoded->operand = (instr>>6)&0x7;
    decoded->execute = execute_ldrsh;
    return execute_ldrsh(pc, decoded);
}

static int execute_str_imm(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;

    if (DISS) fprintf(stderr,"str   r%u,[r%u,#0x%X]",rd,rn,rb);
    rb=read_register(rn)+rb;
    rc=read_register(rd);
    write32(rb,rc); clks++;
    return 0;
}

static int decode_str_imm(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->reg2 = (instr>>3)&0x07;
    decoded->operand = ((instr>>6)&0x1F) << 2;
    decoded->execute = execute_str_imm;
    return execute_str_imm(pc, decoded);
}

static int execute_ldr_imm(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;

    if (DISS) fprintf(stderr,"ldr   r%u,[r%u,#0x%X]",rd,rn,rb);
    rb=read_register(rn)+rb;
    rc=read32(rb); clks++;
    write_register(rd,rc);
    return 0;
}

static int decode_ldr_imm(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->reg2 = (instr>>3)&0x07;
    decoded->operand = ((instr>>6)&0x1F) << 2;
    decoded->execute = execute_ldr_imm;
    return execute_ldr_imm(pc, decoded);
}

static int execute_strb_imm(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;

    if (DISS) fprintf(stderr,"strb  r%u,[r%u,#0x%X]",rd,rn,rb);
    rb=read_register(rn)+rb;
    rc=read_register(rd);
    ra=read16(rb&(~1)); clks++;
    if(rb&1)
    {
        ra&=0x00FF;
        ra|=rc<<8;
    }
    else
    {
        ra&=0xFF00;
        ra|=rc&0x00FF;
    }
    write16(rb&(~1),ra&0xFFFF); clks++;
    return 0;
}

static int decode_strb_imm(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->reg2 = (instr>>3)&0x07;
    decoded->operand = (instr>>6)&0x1F;
    decoded->execute = execute_strb_imm;
    return execute_strb_imm(pc, decoded);
}

static int execute_ldrb_imm(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;

    if (DISS) fprintf(stderr,"ldrb  r%u,[r%u,#0x%X]",rd,rn,rb);
    rb=read_register(rn)+rb;
    rc=read16(rb&(~1)); clks++;
    if(rb&1)
    {
        rc>>=8;
    }

    write_register(rd,rc&0xFF);
    return 0;
}

static int decode_ldrb_imm(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->reg2 = (instr>>3)&0x07;
    decoded->operand = (instr>>6)&0x1F;
    decoded->execute = execute_ldrb_imm;
    return execute_ldrb_imm(pc, decoded);
}

static int execute_strh_imm(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;

    if (DISS) fprintf(stderr,"strh  r%u,[r%u,#0x%X]",rd,rn,rb);
    rb=read_register(rn)+rb;
    rc=read_register(rd);
    write16(rb,rc&0xFFFF); clks++;
    return 0;
}

static int decode_strh_imm(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->reg2 = (instr>>3)&0x07;
    decoded->operand = ((instr>>6)&0x1F) << 1;
    decoded->execute = execute_strh_imm;
    return execute_strh_imm(pc, decoded);
}

static int execute_ldrh_imm(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->reg2;

    if (DISS) fprintf(stderr,"ldrh  r%u,[r%u,#0x%X]",rd,rn,rb);
    rb=read_register(rn)+rb;
    rc=read16(rb); clks++;
    write_register(rd,rc&0xFFFF);
    return 0;
}

static int decode_ldrh_imm(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x07;
    decoded->reg2 = (instr>>3)&0x07;
    decoded->operand = ((instr>>6)&0x1F) << 1;
    decoded->execute = execute_ldrh_imm;
    return execute_ldrh_imm(pc, decoded);
}

static int execute_str_sp(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;

    if (DISS) fprintf(stderr,"str   r%u,[sp,#0x%X]",rd,rb);
    rb=read_register(13)+rb;

    rc=read_register(rd);
    write32(rb,rc); clks++;
    return 0;
}

static int decode_str_sp(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr>>8)&0x07;
    decoded->operand = (instr&0xFF) << 2;
    decoded->execute = execute_str_sp;
    return execute_str_sp(pc, decoded);
}

static int execute_ldr_sp(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;

    if (DISS) fprintf(stderr,"ldr   r%u,[sp+#0x%X]",rd,rb);
    ra=read_register(13);
    //ra&=~3;
    rb+=ra;
    rc=read32(rb); clks++;
    write_register(rd,rc);
    return 0;
}

static int decode_ldr_sp(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr>>8)&0x07;
    decoded->operand = (instr&0xFF) << 2;
    decoded->execute = execute_ldr_sp;
    return execute_ldr_sp(pc, decoded);
}

static int execute_adr(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;

    if (DISS) fprintf(stderr,"add   r%u,pc,#0x%02X",rd,rb);
    ra=read_register(15);
    rc=(ra&(~3))+rb;
    write_register(rd,rc);
    return 0;
}

static int decode_adr(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr>>8)&0x7;
    decoded->operand = (instr&0xFF) << 2;
    decoded->execute = execute_adr;
    return execute_adr(pc, decoded);
}

static int execute_add_from_sp(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;
    unsigned int rd = decoded->reg1;

    if (DISS) fprintf(stderr,"add   r%u,sp,#0x%02X",rd,rb);
    ra=read_register(13);
    rc=ra+rb;
    write_register(rd,rc);
    return 0;
}

static int decode_add_from_sp(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr>>8)&0x7;
    decoded->operand = (instr&0xFF) << 2;
    decoded->execute = execute_add_from_sp;
    return execute_add_from_sp(pc, decoded);
}

static int execute_add_sp(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;
    unsigned int rc;

    if (DISS) fprintf(stderr,"add   sp,#0x%02X",rb);
    ra=read_register(13);
    rc=ra+rb;
    write_register(13,rc);
    return 0;
}

static int decode_add_sp(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = (instr&0x7F) << 2;
    decoded->execute = execute_add_sp;
    return execute_add_sp(pc, decoded);
}

static int execute_sub_sp(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;

    if (DISS) fprintf(stderr,"sub   sp,#0x%02X",rb);
    ra=read_register(13);
    ra-=rb;
    write_register(13,ra);
    return 0;
}

static int decode_sub_sp(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = (instr&0x7F) << 2;
    decoded->execute = execute_sub_sp;
    return execute_sub_sp(pc, decoded);
}

static int execute_sxth(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    rc = read_register(rm) & ((0x8000u << 1) - 1);
    write_register(rd, (rc ^ 0x8000u) - 0x8000u);
    return 0;
}

static int decode_sxth(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr & 7;
    decoded->operand = (instr >> 3) & 7;
    decoded->execute = execute_sxth;
    return execute_sxth(pc, decoded);
}

static int execute_sxtb(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    rc = read_register(rm) & ((0x80u << 1) - 1);
    write_register(rd, (rc ^ 0x80u) - 0x80u);
    return 0;
}

static int decode_sxtb(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr & 7;
    decoded->operand = (instr >> 3) & 7;
    decoded->execute = execute_sxtb;
    return execute_sxtb(pc, decoded);
}

static int execute_uxth(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"uxth  r%u,r%u",rd,rm);
    ra=read_register(rm);
    rc=ra&0xFFFF;
    write_register(rd,rc);
    return 0;
}

static int decode_uxth(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_uxth;
    return execute_uxth(pc, decoded);
}

static int execute_uxtb(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rm = decoded->operand;

    if (DISS) fprintf(stderr,"uxtb  r%u,r%u",rd,rm);
    ra=read_register(rm);
    rc=ra&0xFF;
    write_register(rd,rc);
    return 0;
}

static int decode_uxtb(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_uxtb;
    return execute_uxtb(pc, decoded);
}

static int execute_push(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int sp;
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rd;
    unsigned int registers = decoded->operand;

    if(DISS)
    {
        fprintf(stderr,"push  {");
        for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(registers&rb)
            {
                if(rc) fprintf(stderr,",");
                fprintf(stderr,"r%u",ra);
                rc++;
            }
        }
        if(registers&0x100)
        {
            if(rc) fprintf(stderr,",");
            fprintf(stderr,"lr");
        }
        fprintf(stderr,"}");
    }

    sp=read_register(13);

    for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(registers&rb)
        {
            rc++;
        }
    }
    if(registers&0x100) rc++;
    rc<<=2;
    sp-=rc;
    rd=sp;
    for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(registers&rb)
        {
            write32(rd,read_register(ra)); clks++;
            rd+=4;
        }
    }
    if(registers&0x100)
    {
        rc=read_register(14);
        write32(rd,rc);

        if((rc&1)==0)
        {
            if(DBUGSTACK)fprintf(stderr,"\npush {lr} with an ARM address pc 0x%08X popped 0x%08X\n",pc,rc);

        }

    }
    write_register(13,sp);
    return 0;
}

static int decode_push(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = instr & 0x1FF;
    decoded->execute = execute_push;
    return execute_push(pc, decoded);
}

static int execute_setend(unsigned int pc, const DecodedInstruction *decoded)
{
    fprintf(stderr,"setend not implemented\n");
    return(1);
}

static int decode_setend(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->execute = execute_setend;
    return execute_setend(pc, decoded);
}

static int execute_cpsie(unsigned int pc, const DecodedInstruction *decoded)
{
    primask = 0;
    if (DISS) fprintf(stderr, "cpsie i");
    return 0;
}

static int decode_cpsie(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->execute = execute_cpsie;
    return execute_cpsie(pc, decoded);
}

static int execute_cpsid(unsigned int pc, const DecodedInstruction *decoded)
{
    primask = 1;
    if (DISS) fprintf(stderr, "cpsid i");
    return 0;
}

static int decode_cpsid(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->execute = execute_cpsid;
    return execute_cpsid(pc, decoded);
}

static int execute_rev(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->operand;

    if (DISS) fprintf(stderr,"rev   r%u,r%u",rd,rn);
    ra=read_register(rn);
    rc =((ra>> 0)&0xFF)<<24;
    rc|=((ra>> 8)&0xFF)<<16;
    rc|=((ra>>16)&0xFF)<< 8;
    rc|=((ra>>24)&0xFF)<< 0;
    write_register(rd,rc);
    return 0;
}

static int decode_rev(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_rev;
    return execute_rev(pc, decoded);
}

static int execute_rev16(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->operand;

    if (DISS) fprintf(stderr,"rev16 r%u,r%u",rd,rn);
    ra=read_register(rn);
    rc =((ra>> 0)&0xFF)<< 8;
    rc|=((ra>> 8)&0xFF)<< 0;
    rc|=((ra>>16)&0xFF)<<24;
    rc|=((ra>>24)&0xFF)<<16;
    write_register(rd,rc);
    return 0;
}

static int decode_rev16(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_rev16;
    return execute_rev16(pc, decoded);
}

static int execute_revsh(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rc;
    unsigned int rd = decoded->reg1;
    unsigned int rn = decoded->operand;

    if (DISS) fprintf(stderr,"revsh r%u,r%u",rd,rn);
    ra=read_register(rn);
    rc =((ra>> 0)&0xFF)<< 8;
    rc|=((ra>> 8)&0xFF)<< 0;
    if(rc&0x8000) rc|=0xFFFF0000;
    else          rc&=0x0000FFFF;
    write_register(rd,rc);
    return 0;
}

static int decode_revsh(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = instr&0x7;
    decoded->operand = (instr>>3)&0x7;
    decoded->execute = execute_revsh;
    return execute_revsh(pc, decoded);
}

static int execute_pop(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int sp;
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int registers = decoded->operand;

    if(DISS)
    {
        fprintf(stderr,"pop   {");
        for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(registers&rb)
            {
                if(rc) fprintf(stderr,",");
                fprintf(stderr,"r%u",ra);
                rc++;
            }
        }
        if(registers&0x100)
        {
            if(rc) fprintf(stderr,",");
            fprintf(stderr,"pc");
        }
        fprintf(stderr,"}");
    }

    sp=read_register(13);
    for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(registers&rb)
        {
            write_register(ra,read32(sp)); clks++;
            sp+=4;
        }
    }
    if(registers&0x100)
    {
        rc=read32(sp); clks += 2;
        if((rc&1)==0)
        {
            if(DBUGSTACK)fprintf(stderr,"\npop {rc} with an ARM address pc 0x%08X popped 0x%08X",pc,rc);

            rc&=~1;
        }
        rc+=2;
        write_register(15,rc);
        sp+=4;
    }
    write_register(13,sp);
    return 0;
}

static int decode_pop(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = instr & 0x1FF;
    decoded->execute = execute_pop;
    return execute_pop(pc, decoded);
}

static int execute_bkpt(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;


    clks += 2;
    return without_trace_suffix(handle_bkpt(rb, read32(read_register(13))));
}

static int decode_bkpt(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = instr&0xFF;
    decoded->execute = execute_bkpt;
    return execute_bkpt(pc, decoded);
}

static int execute_yield(unsigned int pc, const DecodedInstruction *decoded)
{
    if (DISS) fprintf(stderr, "yield");
    return 0;
}

static int decode_yield(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->execute = execute_yield;
    return execute_yield(pc, decoded);
}

static int execute_wfe(unsigned int pc, const DecodedInstruction *decoded)
{
    if (DISS) fprintf(stderr, "wfe");
    clks++;
    if (reg_event) reg_event = 0;
    else waiting = WaitForEvt;
    return 0;
}

static int decode_wfe(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->execute = execute_wfe;
    return execute_wfe(pc, decoded);
}

static int execute_wfi(unsigned int pc, const DecodedInstruction *decoded)
{
    if (DISS) fprintf(stderr, "wfi");
    clks++;
    waiting = WaitForInt;
    return 0;
}

static int decode_wfi(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->execute = execute_wfi;
    return execute_wfi(pc, decoded);
}

static int execute_sev(unsigned int pc, const DecodedInstruction *decoded)
{
    if (DISS) fprintf(stderr, "sev");
    reg_event = 1;
    return 0;
}

static int decode_sev(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->execute = execute_sev;
    return execute_sev(pc, decoded);
}

static int execute_nop(unsigned int pc, const DecodedInstruction *decoded)
{
    if (DISS) fprintf(stderr, "nop");
    return 0;
}

static int decode_nop(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->execute = execute_nop;
    return execute_nop(pc, decoded);
}

static int execute_stmia(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int sp;
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rn = decoded->reg1;
    unsigned int registers = decoded->operand;

    if(DISS)
    {
        fprintf(stderr,"stmia r%u!,{",rn);
        for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(registers&rb)
            {
                if(rc) fprintf(stderr,",");
                fprintf(stderr,"r%u",ra);
                rc++;
            }
        }
        fprintf(stderr,"}");
    }
    sp=read_register(rn);
    for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(registers&rb)
        {
            write32(sp,read_register(ra)); clks++;
            sp+=4;
        }
    }
    write_register(rn,sp);
    return 0;
}

static int decode_stmia(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr>>8)&0x7;
    decoded->operand = instr & 0xFF;
    decoded->execute = execute_stmia;
    return execute_stmia(pc, decoded);
}

static int execute_ldmia(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int sp;
    unsigned int ra;
    unsigned int rb;
    unsigned int rc;
    unsigned int rn = decoded->reg1;
    unsigned int registers = decoded->operand;

    if(DISS)
    {
        fprintf(stderr,"ldmia  r%u!,{",rn);
        for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(registers&rb)
            {
                if(rc) fprintf(stderr,",");
                fprintf(stderr,"r%u",ra);
                rc++;
            }
        }
        fprintf(stderr,"}");
    }
    sp=read_register(rn);
    for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(registers&rb)
        {
            write_register(ra,read32(sp)); clks++;
            sp+=4;
        }
    }
    //there is a write back exception.
    if((registers&(1<<rn))==0) write_register(rn,sp);
    return 0;
}

static int decode_ldmia(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->reg1 = (instr>>8)&0x7;
    decoded->operand = instr & 0xFF;
    decoded->execute = execute_ldmia;
    return execute_ldmia(pc, decoded);
}

static int execute_udf(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    if (DISS) fprintf(stderr, "udf #%u\n", rb);
    return without_trace_suffix(hardfault("UDF", pc - 4));
}

static int decode_udf(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = instr & 255;
    decoded->execute = execute_udf;
    return execute_udf(pc, decoded);
}

static int execute_beq(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"beq   0x%08X",rb-3);
    if(cpsr&CPSR_Z)
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_beq(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_beq;
    return execute_beq(pc, decoded);
}

static int execute_bne(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"bne   0x%08X",rb-3);
    if(!(cpsr&CPSR_Z))
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_bne(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_bne;
    return execute_bne(pc, decoded);
}

static int execute_bcs(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"bcs   0x%08X",rb-3);
    if(cpsr&CPSR_C)
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_bcs(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_bcs;
    return execute_bcs(pc, decoded);
}

static int execute_bcc(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"bcc   0x%08X",rb-3);
    if(!(cpsr&CPSR_C))
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_bcc(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_bcc;
    return execute_bcc(pc, decoded);
}

static int execute_bmi(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"bmi   0x%08X",rb-3);
    if(cpsr&CPSR_N)
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_bmi(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_bmi;
    return execute_bmi(pc, decoded);
}

static int execute_bpl(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"bpl   0x%08X",rb-3);
    if(!(cpsr&CPSR_N))
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_bpl(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_bpl;
    return execute_bpl(pc, decoded);
}

static int execute_bvs(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"bvs   0x%08X",rb-3);
    if(cpsr&CPSR_V)
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_bvs(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_bvs;
    return execute_bvs(pc, decoded);
}

static int execute_bvc(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"bvc   0x%08X",rb-3);
    if(!(cpsr&CPSR_V))
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_bvc(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_bvc;
    return execute_bvc(pc, decoded);
}

static int execute_bhi(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"bhi   0x%08X",rb-3);
    if((cpsr&CPSR_C)&&(!(cpsr&CPSR_Z)))
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_bhi(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_bhi;
    return execute_bhi(pc, decoded);
}

static int execute_bls(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"bls   0x%08X",rb-3);
    if((cpsr&CPSR_Z)||(!(cpsr&CPSR_C)))
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_bls(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_bls;
    return execute_bls(pc, decoded);
}

static int execute_bge(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"bge   0x%08X",rb-3);
    ra=0;
    if(  (cpsr&CPSR_N) &&  (cpsr&CPSR_V) ) ra++;
    if((!(cpsr&CPSR_N))&&(!(cpsr&CPSR_V))) ra++;
    if(ra)
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_bge(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_bge;
    return execute_bge(pc, decoded);
}

static int execute_blt(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"blt   0x%08X",rb-3);
    ra=0;
    if((!(cpsr&CPSR_N))&&(cpsr&CPSR_V)) ra++;
    if((!(cpsr&CPSR_V))&&(cpsr&CPSR_N)) ra++;
    if(ra)
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_blt(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_blt;
    return execute_blt(pc, decoded);
}

static int execute_bgt(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"bgt   0x%08X",rb-3);
    ra=0;
    if(  (cpsr&CPSR_N) &&  (cpsr&CPSR_V) ) ra++;
    if((!(cpsr&CPSR_N))&&(!(cpsr&CPSR_V))) ra++;
    if(cpsr&CPSR_Z) ra=0;
    if(ra)
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_bgt(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_bgt;
    return execute_bgt(pc, decoded);
}

static int execute_ble(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int ra;
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr,"ble   0x%08X",rb-3);
    ra=0;
    if((!(cpsr&CPSR_N))&&(cpsr&CPSR_V)) ra++;
    if((!(cpsr&CPSR_V))&&(cpsr&CPSR_N)) ra++;
    if(cpsr&CPSR_Z) ra++;
    if(ra)
    {
        write_register(15,rb); clks++;
    }
    return 0;
}

static int decode_ble(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0xFFu) ^ 0x80u) * 2u - 0x100u;
    decoded->execute = execute_ble;
    return execute_ble(pc, decoded);
}

static int execute_svc(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    unsigned int vector;
    if (DISS) fprintf(stderr, "svc #%u", rb);
    if (!can_take_exception(EXCEPTION_SVC))
        return without_trace_suffix(hardfault("SVCall priority escalation", pc - 2));
    vector = fetch32(entry + 4 * EXCEPTION_SVC);
    if (!valid_vector(vector))
        return without_trace_suffix(hardfault("Invalid SVCall vector", pc - 2));
    write_register(15, enter_exception(EXCEPTION_SVC, pc - 2, vector));
    return 0;
}

static int decode_svc(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = instr & 255;
    decoded->execute = execute_svc;
    return execute_svc(pc, decoded);
}

static int execute_b(unsigned int pc, const DecodedInstruction *decoded)
{
    unsigned int rb = decoded->operand;

    rb += pc + 2;
    if (DISS) fprintf(stderr, "b     0x%08X", rb - 4);
    write_register(15, rb);
    clks++;
    return 0;
}

static int decode_b(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    decoded->operand = ((instr & 0x7FFu) ^ 0x400u) * 2u - 0x800u;
    decoded->execute = execute_b;
    return execute_b(pc, decoded);
}

static int invalid_instruction(unsigned int pc, unsigned int inst, unsigned int second)
{
    fprintf(stderr, "invalid instruction 0x%08X 0x%04X", pc - 4, inst);
    if (inst >= 0xE800) fprintf(stderr, " 0x%04X", second);
    fputc('\n', stderr);
    return 1;
}

static int decode_wide(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    unsigned int inst = instr & 0xFFFF;
    unsigned int second = instr >> 16;

    if (inst == 0xF3EF && (second & 0xF000) == 0x8000) {
        unsigned int rd = (second >> 8) & 15;
        if (rd != 13 && rd != 15)
            return decode_mrs(pc, instr, decoded);
    }
    if ((inst & 0xFFF0) == 0xF380 && (second & 0xFF00) == 0x8800) {
        unsigned int rn = inst & 15;
        if (rn != 13 && rn != 15)
            return decode_msr(pc, instr, decoded);
    }
    if (inst == 0xF3BF) {
        switch (second & 0xFFF0) {
        case 0x8F40: return decode_dsb(pc, instr, decoded);
        case 0x8F50: return decode_dmb(pc, instr, decoded);
        case 0x8F60: return decode_isb(pc, instr, decoded);
        }
    }
    if ((inst & 0xFFF0) == 0xF7F0 && (second & 0xF000) == 0xA000)
        return decode_udf_w(pc, instr, decoded);
    if ((inst & 0xF800) == 0xF000 && (second & 0xD000) == 0xD000)
        return decode_bl(pc, instr, decoded);
    return invalid_instruction(pc, inst, second);
}

static int decode_narrow(unsigned int pc, unsigned int instr, DecodedInstruction *decoded)
{
    unsigned int inst = instr;

    switch (inst & 0xF000) {
    case 0x0000:
        if ((inst & 0xF800) == 0x0000) return decode_lsl_imm(pc, instr, decoded);
        if ((inst & 0xF800) == 0x0800) return decode_lsr_imm(pc, instr, decoded);
        break;
    case 0x1000:
        if ((inst & 0xF800) == 0x1000) return decode_asr_imm(pc, instr, decoded);
        if ((inst & 0xFE00) == 0x1800) return decode_add_reg(pc, instr, decoded);
        if ((inst & 0xFE00) == 0x1A00) return decode_sub_reg(pc, instr, decoded);
        if ((inst & 0xFE00) == 0x1C00 && (inst & 0x01C0))
            return decode_add_imm3(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x1C00) return decode_mov_reg(pc, instr, decoded);
        if ((inst & 0xFE00) == 0x1E00) return decode_sub_imm3(pc, instr, decoded);
        break;
    case 0x2000:
        if ((inst & 0xF800) == 0x2000) return decode_mov_imm(pc, instr, decoded);
        if ((inst & 0xF800) == 0x2800) return decode_cmp_imm(pc, instr, decoded);
        break;
    case 0x3000:
        if ((inst & 0xF800) == 0x3000) return decode_add_imm8(pc, instr, decoded);
        if ((inst & 0xF800) == 0x3800) return decode_sub_imm8(pc, instr, decoded);
        break;
    case 0x4000:
        if ((inst & 0xFFC0) == 0x4000) return decode_and(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x4040) return decode_eor(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x4080) return decode_lsl_reg(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x40C0) return decode_lsr_reg(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x4100) return decode_asr_reg(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x4140) return decode_adc(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x4180) return decode_sbc(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x41C0) return decode_ror(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x4200) return decode_tst(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x4240) return decode_neg(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x4280) return decode_cmp_reg(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x42C0) return decode_cmn(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x4300) return decode_orr(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x4340) return decode_mul(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x4380) return decode_bic(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0x43C0) return decode_mvn(pc, instr, decoded);
        if ((inst & 0xFF00) == 0x4400) return decode_add_high(pc, instr, decoded);
        if ((inst & 0xFF00) == 0x4500) return decode_cmp_high(pc, instr, decoded);
        if ((inst & 0xFF00) == 0x4600) return decode_mov_high(pc, instr, decoded);
        if ((inst & 0xFF87) == 0x4700) return decode_bx(pc, instr, decoded);
        if ((inst & 0xFF87) == 0x4780) return decode_blx_reg(pc, instr, decoded);
        if ((inst & 0xF800) == 0x4800) return decode_ldr_literal(pc, instr, decoded);
        break;
    case 0x5000:
        if ((inst & 0xFE00) == 0x5000) return decode_str_reg(pc, instr, decoded);
        if ((inst & 0xFE00) == 0x5200) return decode_strh_reg(pc, instr, decoded);
        if ((inst & 0xFE00) == 0x5400) return decode_strb_reg(pc, instr, decoded);
        if ((inst & 0xFE00) == 0x5600) return decode_ldrsb(pc, instr, decoded);
        if ((inst & 0xFE00) == 0x5800) return decode_ldr_reg(pc, instr, decoded);
        if ((inst & 0xFE00) == 0x5A00) return decode_ldrh_reg(pc, instr, decoded);
        if ((inst & 0xFE00) == 0x5C00) return decode_ldrb_reg(pc, instr, decoded);
        if ((inst & 0xFE00) == 0x5E00) return decode_ldrsh(pc, instr, decoded);
        break;
    case 0x6000:
        if ((inst & 0xF800) == 0x6000) return decode_str_imm(pc, instr, decoded);
        if ((inst & 0xF800) == 0x6800) return decode_ldr_imm(pc, instr, decoded);
        break;
    case 0x7000:
        if ((inst & 0xF800) == 0x7000) return decode_strb_imm(pc, instr, decoded);
        if ((inst & 0xF800) == 0x7800) return decode_ldrb_imm(pc, instr, decoded);
        break;
    case 0x8000:
        if ((inst & 0xF800) == 0x8000) return decode_strh_imm(pc, instr, decoded);
        if ((inst & 0xF800) == 0x8800) return decode_ldrh_imm(pc, instr, decoded);
        break;
    case 0x9000:
        if ((inst & 0xF800) == 0x9000) return decode_str_sp(pc, instr, decoded);
        if ((inst & 0xF800) == 0x9800) return decode_ldr_sp(pc, instr, decoded);
        break;
    case 0xA000:
        if ((inst & 0xF800) == 0xA000) return decode_adr(pc, instr, decoded);
        if ((inst & 0xF800) == 0xA800) return decode_add_from_sp(pc, instr, decoded);
        break;
    case 0xB000:
        if ((inst & 0xFF80) == 0xB000) return decode_add_sp(pc, instr, decoded);
        if ((inst & 0xFF80) == 0xB080) return decode_sub_sp(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0xB200) return decode_sxth(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0xB240) return decode_sxtb(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0xB280) return decode_uxth(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0xB2C0) return decode_uxtb(pc, instr, decoded);
        if ((inst & 0xFE00) == 0xB400) return decode_push(pc, instr, decoded);
        if ((inst & 0xFFF7) == 0xB650) return decode_setend(pc, instr, decoded);
        if (inst == 0xB662) return decode_cpsie(pc, instr, decoded);
        if (inst == 0xB672) return decode_cpsid(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0xBA00) return decode_rev(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0xBA40) return decode_rev16(pc, instr, decoded);
        if ((inst & 0xFFC0) == 0xBAC0) return decode_revsh(pc, instr, decoded);
        if ((inst & 0xFE00) == 0xBC00) return decode_pop(pc, instr, decoded);
        if ((inst & 0xFF00) == 0xBE00) return decode_bkpt(pc, instr, decoded);
        if ((inst & 0xFF0F) == 0xBF00) {
            switch (inst) {
            case 0xBF10: return decode_yield(pc, instr, decoded);
            case 0xBF20: return decode_wfe(pc, instr, decoded);
            case 0xBF30: return decode_wfi(pc, instr, decoded);
            case 0xBF40: return decode_sev(pc, instr, decoded);
            default: return decode_nop(pc, instr, decoded);
            }
        }
        break;
    case 0xC000:
        if ((inst & 0xF800) == 0xC000) return decode_stmia(pc, instr, decoded);
        if ((inst & 0xF800) == 0xC800) return decode_ldmia(pc, instr, decoded);
        break;
    case 0xD000:
        if ((inst & 0xFF00) == 0xDE00) return decode_udf(pc, instr, decoded);
        switch ((inst >> 8) & 15) {
        case 0x0: return decode_beq(pc, instr, decoded);
        case 0x1: return decode_bne(pc, instr, decoded);
        case 0x2: return decode_bcs(pc, instr, decoded);
        case 0x3: return decode_bcc(pc, instr, decoded);
        case 0x4: return decode_bmi(pc, instr, decoded);
        case 0x5: return decode_bpl(pc, instr, decoded);
        case 0x6: return decode_bvs(pc, instr, decoded);
        case 0x7: return decode_bvc(pc, instr, decoded);
        case 0x8: return decode_bhi(pc, instr, decoded);
        case 0x9: return decode_bls(pc, instr, decoded);
        case 0xA: return decode_bge(pc, instr, decoded);
        case 0xB: return decode_blt(pc, instr, decoded);
        case 0xC: return decode_bgt(pc, instr, decoded);
        case 0xD: return decode_ble(pc, instr, decoded);
        }
        if ((inst & 0xFF00) == 0xDF00) return decode_svc(pc, instr, decoded);
        break;
    case 0xE000:
        if ((inst & 0xF800) == 0xE000) return decode_b(pc, instr, decoded);
        break;
    }
    return invalid_instruction(pc, inst, 0);
}

int execute ( void )
{
    unsigned int pc;
    pc=read_register(15);
    if (active_exception && (pc & 0xFFFFFFF0u) == 0xFFFFFFF0u) {
        unsigned int exc_return = (pc - 2) | 1u;
        if (exc_return != EXC_RETURN_HANDLER && exc_return != EXC_RETURN_THREAD_MSP &&
            exc_return != EXC_RETURN_THREAD_PSP)
            return hardfault("Invalid exception return", pc);
        pc = return_from_exception(exc_return);
    }

    if (systick_pending && can_take_exception(EXCEPTION_SYSTICK))
    {
        unsigned int vector = fetch32(entry + 4 * EXCEPTION_SYSTICK);

        systick_pending = 0;
        systick_ints++;
        pc = enter_exception(EXCEPTION_SYSTICK, pc - 2, vector);
    }


    const unsigned int address = pc - 2;
    // Exclude the end of the ROM region where a wide fetch would cross
    // into unmapped memory, even if a ROM mirror already filled the slot.
    const int cacheable = address < 0x0FFFFFFEu &&
                          !DBUG && !DBUGFETCH && !DBUGREG && !DISS && !output_vcd;
    DecodedInstruction scratch;
    DecodedInstruction *decoded = &scratch;
    unsigned int tag = 0;

    if (cacheable) {
        const unsigned int halfword = (address & ROMADDMASK) >> 1;
        const unsigned int high = halfword >> CACHE_BITS;
        const unsigned int idx = (halfword ^ high) & (CACHE_SIZE - 1);
        decoded = &cache[idx];
        tag = high | CACHE_VALID;

        if ((decoded->tag & ~CACHE_WIDE) == tag) {
            const unsigned int width = (decoded->tag & CACHE_WIDE) ? 2 : 1;
#if CACHE_STATS
            cache_hits++;
#endif
            fetches += width;
            clks += width;
            instructions++;
            pc += 2;
            reg_norm[15] = pc & ~1u;
            return decoded->execute(pc, decoded);
        }

#if CACHE_STATS
        cache_misses++;
#endif
    } else {
#if CACHE_STATS
        uncached_instructions++;
#endif
    }

    const unsigned int inst = fetch16(address);
    clks++;
    pc += 2;
    write_register(15, pc);

    const char *sym = NULL;
    if (DISS) {
        if (DISR) dump_registers();
        fprintf(stderr, "--- 0x%08X: 0x%04X ", pc - 4, inst);
        sym = get_sym(pc - 4);
    }
    if (output_vcd) {
        unsigned int bit;
        fputc('b', fpvcd);
        for (bit = 0x8000; bit; bit >>= 1)
            fputc((bit & inst) ? '1' : '0', fpvcd);
        fprintf(fpvcd, " inst\n");
    }

    instructions++;
    // Invalidate before decoding so unsupported encodings never leave a hit
    // pointing at a previous instruction in this slot.
    decoded->tag = 0;
    decoded->execute = NULL;

    int result;
    if (inst >= 0xE800) {
        const unsigned int second = fetch16(pc - 2);
        clks++;
        if (DISS) fprintf(stderr, "0x%04X ", second);
        result = decode_wide(pc, inst | (second << 16), decoded);
        tag |= CACHE_WIDE;
    } else {
        result = decode_narrow(pc, inst, decoded);
    }

    if (cacheable && decoded->execute)
        decoded->tag = tag;

    if (DISS && result == 0)
        fprintf(stderr, sym ? " ; %s\n" : "\n", sym);
    return result == NO_TRACE_SUFFIX ? 0 : result;
}
//-------------------------------------------------------------------
int reset ( void )
{
    unsigned int tenms;

    memset(ram,0x00,sizeof(ram));

    systick_ctrl=SYSTICK_CLKSOURCE;
    systick_reload=0x00000000;
    systick_count=0x00000000;
    /*--------------CALIB-------------*/
    systick_calibrate=CALIB_NOREF;
    if (cpuhz % 100)
        systick_calibrate |= CALIB_SKEW;
    tenms = cpuhz / 100;
    if (tenms < 0x01000000)
        systick_calibrate |= tenms;
    /*--------------------------------*/
    active_exception=0;
    systick_pending=0;
    primask=0;
    psp=0;
    control=0;
    shpr2=0;
    shpr3=0;
    cpsr=0;
    reg_event=0;
    waiting=NoWait;

    reg_norm[13]=fetch32(entry); //cortex-m
    reg_norm[14]=0xFFFFFFFF;
    reg_norm[15]=fetch32(entry+4); //cortex-m
    if((reg_norm[15]&1)==0)
    {
        fprintf(stderr,"reset vector with an ARM address 0x%08X\n",reg_norm[15]);
        exit(1);
    }
    reg_norm[15]&=~1;
    reg_norm[15]+=2;

    clks=0;
    instructions=0;
    fetches=0;
    reads=0;
    writes=0;

    return(0);
}
//-------------------------------------------------------------------
static void poll_host_input(int *input_open)
{
    char c;
    ssize_t n;

    while (frame_buffer && fb_qevent())
        fb_event();
    if (*input_open) {
        while ((n = read(read_fd, &c, 1)) == 1)
            input_char(c, EvtKey, -1, -1);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
            *input_open = 0;
    }
}

static void wait_for_host_input(int input_open)
{
    fd_set fds;
    int nfds = 0;

    FD_ZERO(&fds);
    if (input_open) {
        FD_SET(read_fd, &fds);
        nfds = read_fd + 1;
    }
#if HAVE_X11
    if (frame_buffer) {
        int xfd = ConnectionNumber(xDisplay);
        FD_SET(xfd, &fds);
        if (xfd >= nfds) nfds = xfd + 1;
    }
#endif
    select(nfds, &fds, NULL, NULL, NULL);
}

int run ( void )
{
    int input_open = 1;

    esp_sockets_init();
    esp_reset();
    reset();
    while(1)
    {
        unsigned long previous_clks;

        if (waiting != NoWait || (instructions & 0x7F) == 0)
            poll_host_input(&input_open);

        if (waiting != NoWait) {
            // Queued console input can wake WFI without a guest IRQ vector.
            int irq_wakes = waiting == WaitForInt ?
                exception_priority(EXCEPTION_SYSTICK) < exception_priority(active_exception) :
                can_take_exception(EXCEPTION_SYSTICK);
            int wake = (irq_wakes && systick_pending) ||
                       (waiting == WaitForInt && input_read_ptr != input_write_ptr);

            if (waiting == WaitForEvt && reg_event) {
                reg_event = 0;
                wake = 1;
            }
            if (!wake && irq_wakes &&
                (systick_ctrl & (SYSTICK_ENABLE | SYSTICK_TICKINT)) ==
                (SYSTICK_ENABLE | SYSTICK_TICKINT) &&
                (systick_count != 0 || systick_reload != 0)) {
                unsigned int ticks = systick_count ? systick_count : systick_reload + 1;

                // Advance to the interrupt without fetching any instructions.
                clks += ticks;
                advance_systick(ticks);
                wake = 1;
            }
            if (!wake) {
                wait_for_host_input(input_open);
                continue;
            }
            waiting = NoWait;
        }

        if(output_vcd)
        {
            fprintf(fpvcd,"#%u\n",vcdcount++);
        }
        previous_clks = clks;
        if (execute()) break;
        advance_systick(clks - previous_clks);
    }
    esp_close();
    dump_counters();
    return(0);
}
//-------------------------------------------------------------------
unsigned int load_binary(unsigned int addr, const char *name)
{
	int f;
	int r;
	struct stat st;

	f = open(name, O_RDONLY);
	if (f < 0) {
		perror("Can't open file:");
		exit(1);
	}
	fstat(f, &st);
    r = st.st_size;
	memset(&rom[addr >> 1], 0x0a0a, (r + 1) & ~0x1);
	r = read(f, &rom[addr >> 1], r);
	close(f);
	return (r + 1) & ~0x1;
}
//-------------------------------------------------------------------
unsigned int htoi(const char *h)
{
	unsigned int r = 0;
    char c;

	while (*h) {
		r <<= 4;
		c = toupper(*h);
		r += c - (c > '9' ? 55 : 48);
		h++;
	}
	return r;
}
//-------------------------------------------------------------------
int start_server(int port)
{
	struct sockaddr_in server, client;
	socklen_t c;
	int t = 1;

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);
	if (bind(socket_fd, (struct sockaddr *) &server, sizeof(server)) < 0) {
		perror("Failed to bind");
		exit(1);
	}
	if (listen(socket_fd, 1) < 0) {
		perror("Failed to listen");
		exit(1);
	}
	setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &t,sizeof(int));
	c = sizeof(struct sockaddr_in);
	fprintf(stderr, "Waiting for connection\n");
	read_fd = accept(socket_fd, (struct sockaddr *) &client, (socklen_t *) &c);
	write_fd = read_fd;
	fprintf(stderr, "Connected\n");
    return 0;
}
//-------------------------------------------------------------------
void stop_server(void)
{
	shutdown(read_fd, SHUT_RDWR);
	close(read_fd);
	close(socket_fd);
}
//-------------------------------------------------------------------
const char options[] =
#if HAVE_X11
	"c:d:e:f:g:h:m:o:p:rsv?"
#else
	"c:d:e:f:h:m:o:p:rsv?"
#endif
;
//-------------------------------------------------------------------
static void usage()
{
    fprintf(stderr, "usage: thumbulator [options] <rom.bin>...\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "    -c <cpuid>         set CPUID\n");
    fprintf(stderr, "    -d <dump.out>      set dump output file name\n");
    fprintf(stderr, "    -e <entry>         set entry address\n");
    fprintf(stderr, "    -f <freq>          set CPU frequency in Hz (default 8MHz)\n");
#if HAVE_X11
    fprintf(stderr, "    -g WxH             init gfx framebuffer\n");
#endif
    fprintf(stderr, "    -h <disk.img>      set next disk image\n");
    fprintf(stderr, "    -m <symbol.map>    load symbols (fmt: sym addr)\n");
    fprintf(stderr, "    -o <org>           set load address\n");
    fprintf(stderr, "    -p <port>          start TCP server on <port>\n");
    fprintf(stderr, "    -r                 dump registers in disassembly\n");
    fprintf(stderr, "    -s                 enable disassembly\n");
    fprintf(stderr, "    -v                 enable VCD output\n");
    fprintf(stderr, "    -?                 help\n");
    exit(1);
}
//-------------------------------------------------------------------
void handle_cmd_line(int argc, char *argv[])
{
	int i, c, fd;
    const char *p, *opt, *optarg;
	unsigned int org = 0;
    off_t off;

    if (1 == argc)
        usage();

    for (i = 1; i < argc; i++) {
        opt    = argv[i];
        optarg = i+1 < argc ? argv[i+1] : NULL;
        if (opt[0] != '-') {
            org += load_binary(org, opt);
            continue;
        }
        c = opt[1];
        p = strchr(options, opt[1]);
        if (p && p[1] == ':') {
            if (NULL == optarg) {
                fprintf(stderr, "No argument for %s\n", opt);
                exit(1);
            }
            i++;
        }
		switch (c) {
		case 'c': cpuid = htoi(optarg); break;
		case 'd': output_file_name = optarg; break;
		case 'e': entry = htoi(optarg); break;
        case 'f': cpuhz = atoi(optarg); break;
#if HAVE_X11
        case 'g':
            {
                char tmp[128];
                int mem_size;

                strcpy(tmp, optarg);
                p = strchr(tmp, 'x');
                if (NULL == p) {
                    fprintf(stderr,"Define the resolution as WxH!");
                    exit(1);
                }
                *p = 0;
                fb_width  = atoi(tmp); fb_height = atoi(p + 1);
                mem_size = fb_width * fb_height * 4;
                if (mem_size > 15*1024*1024) {
                    fprintf(stderr, "Max. 15MB frame buffer supported!");
                    exit(1);
                }
                frame_buffer = malloc(mem_size);
                if (NULL == frame_buffer) {
                    fprintf(stderr, "Cannot allocate frame buffer!");
                    exit(1);
                }
            }
            break;
#endif
        case 'h':
            if (disk_count == MAX_DISK) {
                fprintf(stderr, "Not enough disk slots (max. %d)!", MAX_DISK);
                exit(1);
            }
            fd = open(optarg, O_RDWR);
            if (fd < 0) {
                fprintf(stderr, "Cannot open %s!", optarg);
                exit(1);
            }
            disk_fd[disk_count] = fd;
            off = lseek(fd, 0, SEEK_END);
            disk_size[disk_count++] = off - sizeof(disk_buffer);
            break;
		case 'm': load_syms(optarg); break;
		case 'o': org = htoi(optarg); break;
		case 'p': start_server(atoi(optarg)); break;
        case 'r': DISR = 1; break;
        case 's': DISS = 1; break;
        case 'v': output_vcd = 1; break;
        case '?':
        default:  usage(); break;
		}
	}
}
//-------------------------------------------------------------------
int main ( int argc, char *argv[] )
{
    int ra, flags;

    init_syms();
    output_vcd=0;
    cpuid = 0;
    cpuhz = 8000000;
    read_fd = STDIN_FILENO;
    write_fd = STDOUT_FILENO;
    error_fd = STDERR_FILENO;
    memset(rom,0xFF,sizeof(rom));
    memset(ram,0x00,sizeof(ram));
    frame_buffer = NULL;
    fb_width = fb_height = 0;
    disk_count = 0;
    for (ra = 0; ra < MAX_DISK; ra++)
        disk_fd[ra] = -1;
    handle_cmd_line(argc, argv);
    flags = fcntl(read_fd, F_GETFL, 0);
    fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);

    if(output_vcd)
    {
        fprintf(stderr,"output vcd enabled\n");
        fpvcd=fopen("output.vcd","wt");
        if(fpvcd==NULL)
        {
            fprintf(stderr,"Error creating file output.vcd\n");
            output_vcd=0;
            return(1);
        }
        fprintf(fpvcd,"$version Generated by thumbulator $end\n");
        fprintf(fpvcd,"$timescale 1ns $end\n");
        fprintf(fpvcd,"$scope module thumb $end\n");
        for(ra=0;ra<16;ra++)
        {
            fprintf(fpvcd,"$var wire 32 r%u r%u $end\n",ra,ra);
        }
        fprintf(fpvcd,"$var wire 16 inst inst $end\n");
        fprintf(fpvcd,"$upscope $end\n");
        fprintf(fpvcd,"$enddefinitions $end\n");
        vcdcount=0;
        fprintf(fpvcd,"#%u\n",vcdcount++);
        for(ra=0;ra<16;ra++)
        {
            fprintf(fpvcd,"b0 r%u\n",ra);
        }
        fprintf(fpvcd,"b0 inst\n");
    }

    if (frame_buffer)
        fb_open(fb_width, fb_height, (char*) frame_buffer);

    run();
    if (socket_fd != -1)
        stop_server();

    if(output_vcd)
    {
        fclose(fpvcd);
    }
    if (frame_buffer)
        fb_close();
    for (ra = 0; ra < MAX_DISK; ra++)
        if (disk_fd[ra] >= 0)
            close(disk_fd[ra]);
    return(0);
}
//-------------------------------------------------------------------
//-------------------------------------------------------------------
//-------------------------------------------------------------------



//-------------------------------------------------------------------------
//
// Copyright (c) 2010 David Welch dwelch@dwelch.com
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------


// vim:ts=4 sw=4 et:
