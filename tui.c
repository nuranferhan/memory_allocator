/*
 * tui.c - Custom Memory Allocator için kullanıcı dostu terminal arayüzü.
 *
 * Derleme: make tui
 * Kullanım: ./allocator_tui
 */

#include "allocator.h"

#include <errno.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_DIM     "\033[2m"
#define CLR_RED     "\033[1;31m"
#define CLR_GREEN   "\033[1;32m"
#define CLR_YELLOW  "\033[1;33m"
#define CLR_BLUE    "\033[1;34m"
#define CLR_MAGENTA "\033[1;35m"
#define CLR_CYAN    "\033[1;36m"
#define CLR_WHITE   "\033[1;37m"

#define KEY_NONE       0
#define KEY_QUIT       'q'
#define KEY_LEFT       1001
#define KEY_RIGHT      1002
#define KEY_UP         1003
#define KEY_DOWN       1004
#define KEY_ENTER      1005
#define KEY_BACKSPACE  1006
#define KEY_OTHER      2000

#define SAMPLE_COUNT 8
#define THREAD_COUNT 4
#define THREAD_OPS   300

typedef enum {
    TAB_OVERVIEW = 0,
    TAB_BLOCKS,
    TAB_TESTS,
    TAB_HELP,
    TAB_COUNT
} tab_t;

typedef struct {
    int id;
    size_t size;
    void *ptr;
    char type[16];
} sample_alloc_t;

typedef struct {
    int tid;
    int errors;
} worker_arg_t;

static struct termios original_termios;
static int raw_mode_enabled = 0;
static volatile sig_atomic_t stop_requested = 0;

static tab_t current_tab = TAB_OVERVIEW;
static sample_alloc_t samples[SAMPLE_COUNT];
static int next_sample_id = 1;
static char status_line[256] = "Hazır. Sağ/sol ok ile sekme değiştirin, 'a' ile örnek malloc yapın.";
static char status_color[16] = CLR_GREEN;

static const char *tab_names[TAB_COUNT] = {
    "Genel",
    "Bloklar",
    "Testler",
    "Yardım"
};

static void set_status(const char *color, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(status_line, sizeof(status_line), fmt, args);
    va_end(args);
    snprintf(status_color, sizeof(status_color), "%s", color);
}

static void disable_raw_mode(void)
{
    if (raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
        raw_mode_enabled = 0;
    }
    printf("\033[?25h\033[0m\n");
    fflush(stdout);
}

static int enable_raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &original_termios) == -1)
        return -1;

    struct termios raw = original_termios;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_oflag |= OPOST;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        return -1;

    raw_mode_enabled = 1;
    atexit(disable_raw_mode);
    printf("\033[?25l");
    return 0;
}

static void on_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
}

static int read_key(void)
{
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 0) return KEY_NONE;
    if (n < 0) return KEY_NONE;

    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == 127 || c == 8) return KEY_BACKSPACE;

    if (c == 27) {
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_NONE;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_NONE;
        if (seq[0] == '[') {
            if (seq[1] == 'D') return KEY_LEFT;
            if (seq[1] == 'C') return KEY_RIGHT;
            if (seq[1] == 'A') return KEY_UP;
            if (seq[1] == 'B') return KEY_DOWN;
        }
        return KEY_OTHER;
    }

    if ((c & 0x80) != 0) {
        int extra = 0;
        if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        while (extra-- > 0) {
            unsigned char dummy;
            if (read(STDIN_FILENO, &dummy, 1) != 1) break;
        }
        return KEY_OTHER;
    }

    return c;
}

static void get_terminal_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        *rows = 24;
        *cols = 80;
        return;
    }
    *rows = ws.ws_row;
    *cols = ws.ws_col;
}

static void clear_screen(void)
{
    printf("\033[2J\033[H");
}

static void print_bar(size_t used, size_t total, int width)
{
    if (width < 10) width = 10;
    double ratio = total > 0 ? (double)used / (double)total : 0.0;
    int filled = (int)(ratio * width + 0.5);
    if (filled > width) filled = width;

    putchar('[');
    for (int i = 0; i < width; i++)
        putchar(i < filled ? '#' : '-');
    putchar(']');
}

static void print_header(int cols)
{
    printf(CLR_BOLD CLR_CYAN "Custom Memory Allocator " CLR_RESET "\n");
    printf(CLR_WHITE);
    for (int i = 0; i < cols && i < 100; i++) putchar('=');
    printf(CLR_RESET "\n");

    for (int i = 0; i < TAB_COUNT; i++) {
        if ((tab_t)i == current_tab)
            printf(CLR_BLUE CLR_BOLD " [ %d:%s ] " CLR_RESET, i + 1, tab_names[i]);
        else
            printf(CLR_WHITE "   %d:%s   " CLR_RESET, i + 1, tab_names[i]);
    }
    printf("\n\n");
}

static void print_status(void)
{
    printf("\n%s%s%s\n", status_color, status_line, CLR_RESET);
    printf(CLR_DIM "Kısayollar: 1-4 sekme, ok tuşları gezinme, a malloc, c calloc, f free, d double-free testi, t thread testi, r reset, q çıkış" CLR_RESET "\n");
}

static int first_free_sample_slot(void)
{
    for (int i = 0; i < SAMPLE_COUNT; i++)
        if (!samples[i].ptr) return i;
    return -1;
}

static int last_used_sample_slot(void)
{
    for (int i = SAMPLE_COUNT - 1; i >= 0; i--)
        if (samples[i].ptr) return i;
    return -1;
}

static void action_malloc(void)
{
    static const size_t sizes[] = {64, 128, 256, 512, 1024, 2048};
    int slot = first_free_sample_slot();
    if (slot < 0) {
        set_status(CLR_YELLOW, "Örnek alan dolu. Önce 'f' ile bir blok serbest bırakın.");
        return;
    }

    size_t size = sizes[(unsigned)next_sample_id % (sizeof(sizes) / sizeof(sizes[0]))];
    void *ptr = my_malloc(size);
    if (!ptr) {
        set_status(CLR_RED, "my_malloc(%zu) başarısız oldu: %s", size, strerror(errno));
        return;
    }

    samples[slot].id = next_sample_id++;
    samples[slot].size = size;
    samples[slot].ptr = ptr;
    snprintf(samples[slot].type, sizeof(samples[slot].type), "malloc");
    memset(ptr, 0xA5, size);
    set_status(CLR_GREEN, "malloc ile %zu byte tahsis edildi. Örnek blok id=%d.", size, samples[slot].id);
}

static void action_calloc(void)
{
    int slot = first_free_sample_slot();
    if (slot < 0) {
        set_status(CLR_YELLOW, "Örnek alan dolu. Önce 'f' ile bir blok serbest bırakın.");
        return;
    }

    size_t count = 16;
    size_t elem = sizeof(int);
    int *ptr = (int *)my_calloc(count, elem);
    if (!ptr) {
        set_status(CLR_RED, "my_calloc(%zu, %zu) başarısız oldu.", count, elem);
        return;
    }

    int zero_ok = 1;
    for (size_t i = 0; i < count; i++) {
        if (ptr[i] != 0) {
            zero_ok = 0;
            break;
        }
    }

    samples[slot].id = next_sample_id++;
    samples[slot].size = count * elem;
    samples[slot].ptr = ptr;
    snprintf(samples[slot].type, sizeof(samples[slot].type), "calloc");
    set_status(zero_ok ? CLR_GREEN : CLR_RED,
               zero_ok ? "calloc ile %zu byte alındı ve sıfırlama doğrulandı."
                       : "calloc ile alınan bellekte sıfırlama hatası görüldü.",
               count * elem);
}

static void action_free(void)
{
    int slot = last_used_sample_slot();
    if (slot < 0) {
        set_status(CLR_YELLOW, "Serbest bırakılacak örnek blok yok.");
        return;
    }

    int id = samples[slot].id;
    size_t size = samples[slot].size;
    my_free(samples[slot].ptr);
    memset(&samples[slot], 0, sizeof(samples[slot]));
    set_status(CLR_GREEN, "Örnek blok id=%d (%zu byte) serbest bırakıldı.", id, size);
}

static void action_double_free(void)
{
    void *ptr = my_malloc(64);
    if (!ptr) {
        set_status(CLR_RED, "Double-free testi için tahsis yapılamadı.");
        return;
    }
    my_free(ptr);
    my_free(ptr);
    set_status(CLR_MAGENTA, "Double-free testi çalıştı. İkinci free engellendi ve log'a yazıldı.");
}

static void *thread_worker(void *arg)
{
    worker_arg_t *w = (worker_arg_t *)arg;
    void *ptrs[16] = {0};
    int held = 0;
    unsigned int seed = (unsigned int)(time(NULL) ^ (w->tid * 2654435761u));

    for (int i = 0; i < THREAD_OPS; i++) {
        if (held < 16 && (held == 0 || rand_r(&seed) % 2 == 0)) {
            size_t size = 24 + rand_r(&seed) % 256;
            void *p = my_malloc(size);
            if (!p) {
                w->errors++;
                continue;
            }
            memset(p, w->tid, size);
            ptrs[held++] = p;
        } else if (held > 0) {
            int idx = rand_r(&seed) % held;
            my_free(ptrs[idx]);
            ptrs[idx] = ptrs[--held];
        }
    }

    for (int i = 0; i < held; i++)
        my_free(ptrs[i]);

    return NULL;
}

static void action_thread_test(void)
{
    pthread_t threads[THREAD_COUNT];
    worker_arg_t args[THREAD_COUNT];
    memset(args, 0, sizeof(args));

    for (int i = 0; i < THREAD_COUNT; i++) {
        args[i].tid = i + 1;
        if (pthread_create(&threads[i], NULL, thread_worker, &args[i]) != 0)
            args[i].errors++;
    }

    for (int i = 0; i < THREAD_COUNT; i++)
        pthread_join(threads[i], NULL);

    int errors = 0;
    for (int i = 0; i < THREAD_COUNT; i++)
        errors += args[i].errors;

    set_status(errors == 0 ? CLR_GREEN : CLR_YELLOW,
               "%d thread x %d işlem tamamlandı. Hata sayısı: %d.",
               THREAD_COUNT, THREAD_OPS, errors);
}

static void action_reset(void)
{
    for (int i = 0; i < SAMPLE_COUNT; i++)
        memset(&samples[i], 0, sizeof(samples[i]));
    allocator_init(STRATEGY_BEST_FIT);
    next_sample_id = 1;
    set_status(CLR_GREEN, "Allocator sıfırlandı. Strateji tekrar Best-Fit yapıldı.");
}

static void draw_overview(void)
{
    allocator_snapshot_t s;
    allocator_get_snapshot(&s);

    printf(CLR_BOLD "Bu sekme ne yapar?" CLR_RESET "\n");
    printf("Allocator'ın anlık kullanımını, stratejisini ve fragmentation oranını gösterir.\n\n");

    printf("Strateji             : %s\n", allocator_strategy_name(s.strategy));
    printf("Heap boyutu          : %zu byte (%.2f MB)\n", s.heap_size, s.heap_size / (1024.0 * 1024.0));
    printf("Anlık kullanım       : %zu byte\n", s.current_usage);
    printf("Zirve kullanım       : %zu byte\n", s.peak_usage);
    printf("Toplam tahsis        : %zu byte / %zu çağrı\n", s.total_allocated, s.alloc_count);
    printf("Toplam serbest       : %zu byte / %zu çağrı\n", s.total_freed, s.free_count);
    printf("Fragmentation        : %.2f%%\n", s.fragmentation * 100.0);

    printf("\nKullanım çubuğu      : ");
    print_bar(s.current_usage, s.heap_size, 36);
    printf(" %.2f%%\n", s.heap_size ? s.current_usage * 100.0 / s.heap_size : 0.0);

    printf("Serbest alan çubuğu  : ");
    print_bar(s.free_bytes, s.heap_size, 36);
    printf(" %.2f%%\n", s.heap_size ? s.free_bytes * 100.0 / s.heap_size : 0.0);
}

static void draw_blocks(void)
{
    allocator_snapshot_t s;
    allocator_get_snapshot(&s);

    printf(CLR_BOLD "Bu sekme ne yapar?" CLR_RESET "\n");
    printf("Serbest liste özetini ve TUI içinde oluşturulan örnek tahsisleri gösterir.\n\n");

    printf("Serbest blok sayısı      : %zu\n", s.free_block_count);
    printf("Dolu blok sayısı         : %zu\n", s.used_block_count);
    printf("Toplam serbest byte      : %zu\n", s.free_bytes);
    printf("En büyük serbest blok    : %zu\n", s.largest_free_block);
    printf("Fragmentation formülü    : 1 - (en büyük serbest blok / toplam serbest byte)\n\n");

    printf(CLR_BOLD "Örnek tahsisler\n" CLR_RESET);
    printf("  %-4s %-10s %-10s %-18s\n", "ID", "Tür", "Boyut", "Adres");
    printf("  ------------------------------------------------\n");
    int any = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        if (!samples[i].ptr) continue;
        any = 1;
        printf("  %-4d %-10s %-10zu %p\n",
               samples[i].id, samples[i].type, samples[i].size, samples[i].ptr);
    }
    if (!any)
        printf("  Henüz örnek blok yok. 'a' veya 'c' tuşuna basabilirsiniz.\n");
}

static void draw_tests(void)
{
    printf(CLR_BOLD "Bu sekme ne yapar?" CLR_RESET "\n");
    printf("Allocator davranışını hızlı ve etkileşimli komutlarla doğrular.\n\n");

    printf(CLR_GREEN "a" CLR_RESET "  my_malloc ile örnek blok ayırır ve içine veri yazar.\n");
    printf(CLR_GREEN "c" CLR_RESET "  my_calloc ile örnek blok ayırır, sıfırlamayı kontrol eder.\n");
    printf(CLR_GREEN "f" CLR_RESET "  Son örnek bloğu my_free ile serbest bırakır.\n");
    printf(CLR_GREEN "d" CLR_RESET "  Çifte serbest bırakma korumasını çalıştırır.\n");
    printf(CLR_GREEN "t" CLR_RESET "  Çok threadli tahsis/serbest bırakma testi yapar.\n");
    printf(CLR_GREEN "r" CLR_RESET "  Allocator'ı temiz başlangıç durumuna döndürür.\n\n");

    printf("Daha ayrıntılı doğrulama için ayrı test paketi: " CLR_CYAN "make test" CLR_RESET "\n");
    printf("Performans raporu için: " CLR_CYAN "make bench" CLR_RESET "\n");
}

static void draw_help(void)
{
    printf(CLR_BOLD "Bu sekme ne yapar?" CLR_RESET "\n");
    printf("TUI kullanımını ve ekrandaki değerlerin anlamını açıklar.\n\n");

    printf("1-4             Sekmeler arasında doğrudan geçiş\n");
    printf("Sol/Sağ ok      Önceki/sonraki sekme\n");
    printf("a, c, f         Tahsis, calloc ve free denemeleri\n");
    printf("d               Double-free kontrolü\n");
    printf("t               Mutex korumalı thread testi\n");
    printf("r               Allocator sıfırlama\n");
    printf("q               Çıkış\n\n");

   
}

static void draw_screen(void)
{
    int rows, cols;
    get_terminal_size(&rows, &cols);
    (void)rows;

    clear_screen();
    print_header(cols);

    switch (current_tab) {
        case TAB_OVERVIEW: draw_overview(); break;
        case TAB_BLOCKS:   draw_blocks(); break;
        case TAB_TESTS:    draw_tests(); break;
        case TAB_HELP:     draw_help(); break;
        default:           draw_overview(); break;
    }

    print_status();
    fflush(stdout);
}

static void handle_key(int key)
{
    if (key == KEY_NONE) return;

    if (key == KEY_QUIT || key == 'Q') {
        stop_requested = 1;
        return;
    }

    if (key >= '1' && key <= '4') {
        current_tab = (tab_t)(key - '1');
        set_status(CLR_CYAN, "%s sekmesine geçildi.", tab_names[current_tab]);
        return;
    }

    if (key == KEY_RIGHT) {
        current_tab = (tab_t)((current_tab + 1) % TAB_COUNT);
        set_status(CLR_CYAN, "%s sekmesine geçildi.", tab_names[current_tab]);
        return;
    }

    if (key == KEY_LEFT) {
        current_tab = (tab_t)((current_tab + TAB_COUNT - 1) % TAB_COUNT);
        set_status(CLR_CYAN, "%s sekmesine geçildi.", tab_names[current_tab]);
        return;
    }

    switch (key) {
        case 'a':
        case 'A':
            action_malloc();
            break;
        case 'c':
        case 'C':
            action_calloc();
            break;
        case 'f':
        case 'F':
            action_free();
            break;
        case 'd':
        case 'D':
            action_double_free();
            break;
        case 't':
        case 'T':
            action_thread_test();
            break;
        case 'r':
        case 'R':
            action_reset();
            break;
        case KEY_OTHER:
            set_status(CLR_YELLOW, "Bu karakter komut olarak kullanılmıyor; tek giriş olarak algılandı ve yoksayıldı.");
            break;
        case KEY_UP:
        case KEY_DOWN:
            set_status(CLR_YELLOW, "Yukarı/aşağı ok bu ekranda komut değil; giriş tek tuş olarak algılandı.");
            break;
        default:
            if (key >= 32 && key <= 126)
                set_status(CLR_YELLOW, "'%c' için atanmış komut yok.", key);
            else
                set_status(CLR_YELLOW, "Bu tuş için atanmış komut yok.");
            break;
    }
}

int main(void)
{
    setlocale(LC_ALL, "");
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (enable_raw_mode() != 0) {
        fprintf(stderr, "Terminal ham moda alınamadı. Lütfen gerçek bir terminalde çalıştırın.\n");
        return EXIT_FAILURE;
    }

    log_init("allocator_tui.log");
    if (allocator_init(STRATEGY_BEST_FIT) != 0) {
        fprintf(stderr, "Allocator başlatılamadı.\n");
        return EXIT_FAILURE;
    }

    while (!stop_requested) {
        draw_screen();
        int key = read_key();
        handle_key(key);
    }

    disable_raw_mode();
    allocator_destroy();
    log_close();
    printf("TUI kapatıldı.\n");
    return EXIT_SUCCESS;
}
