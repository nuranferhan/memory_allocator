/*
 * benchmark.c - Performans karşılaştırma programı
 *
 * Ölçülen senaryolar:
 *   B1 - First-Fit vs Best-Fit throughput
 *   B2 - Thread sayısına göre hız değişimi
 *   B3 - Farklı blok boyutlarında throughput
 *   B4 - Fragmentation yük altında
 */

#include "allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define C_BLD "\033[1m"
#define C_CYN "\033[0;36m"
#define C_YEL "\033[0;33m"
#define C_RST "\033[0m"

static double elapsed_ms(struct timespec s, struct timespec e)
{
    return (e.tv_sec - s.tv_sec) * 1000.0 + (e.tv_nsec - s.tv_nsec) / 1e6;
}

/* ─── B1: First-Fit vs Best-Fit ─────────────────────────────── */
static void bench_strategy(alloc_strategy_t strategy, const char *name,
                            int iters, double *out_ms)
{
    allocator_destroy();
    allocator_init(strategy);

    void **ptrs = malloc(sizeof(void *) * iters);
    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < iters; i++)
        ptrs[i] = my_malloc(16 + rand() % 512);
    for (int i = 0; i < iters; i++)
        my_free(ptrs[i]);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    *out_ms = elapsed_ms(t0, t1);
    printf("  %-12s : %6.2f ms  (%5.1f ns/op)\n",
           name, *out_ms, *out_ms * 1e6 / iters);

    free(ptrs);
}

/* ─── B2: Thread ölçeklenebilirlik ──────────────────────────── */
typedef struct { int ops; double ms; } bench_thread_arg_t;

static void *bench_thread_fn(void *arg)
{
    bench_thread_arg_t *a = (bench_thread_arg_t *)arg;
    struct timespec t0, t1;
    void *ptrs[64];
    int held = 0;
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)pthread_self();

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < a->ops; i++) {
        if (held < 64 && (held == 0 || rand_r(&seed) % 2)) {
            void *p = my_malloc(64 + rand_r(&seed) % 128);
            if (p) ptrs[held++] = p;
        } else if (held > 0) {
            int idx = rand_r(&seed) % held;
            my_free(ptrs[idx]);
            ptrs[idx] = ptrs[--held];
        }
    }
    for (int i = 0; i < held; i++) my_free(ptrs[i]);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    a->ms = elapsed_ms(t0, t1);
    return NULL;
}

static void bench_scalability(void)
{
    int thread_counts[] = {1, 2, 4, 8, 16};
    int n = sizeof(thread_counts) / sizeof(thread_counts[0]);
    int ops_per_thread = 2000;

    printf(C_BLD C_CYN "\nB2: Thread Sayisina Gore Olcekleme\n" C_RST);
    printf("  %-8s  %-10s  %-12s  %-12s\n",
           "Thread", "ToplamOps", "Sure(ms)", "Verim(Kops/s)");
    printf("  %s\n", "--------------------------------------------");

    double baseline_tp = 0;
    for (int t = 0; t < n; t++) {
        int tc = thread_counts[t];

        allocator_destroy();
        allocator_init(STRATEGY_BEST_FIT);

        pthread_t          *threads = malloc(sizeof(pthread_t) * tc);
        bench_thread_arg_t *args    = malloc(sizeof(bench_thread_arg_t) * tc);

        struct timespec wall0, wall1;
        clock_gettime(CLOCK_MONOTONIC, &wall0);

        for (int i = 0; i < tc; i++) {
            args[i].ops = ops_per_thread;
            args[i].ms  = 0;
            pthread_create(&threads[i], NULL, bench_thread_fn, &args[i]);
        }
        for (int i = 0; i < tc; i++)
            pthread_join(threads[i], NULL);

        clock_gettime(CLOCK_MONOTONIC, &wall1);

        double wall_ms    = elapsed_ms(wall0, wall1);
        int    total_ops  = tc * ops_per_thread;
        double throughput = total_ops / (wall_ms / 1000.0) / 1000.0;

        if (tc == 1) baseline_tp = throughput;
        double speedup = throughput / baseline_tp;

        printf("  %-8d  %-10d  %-12.2f  %-10.1f  (x%.2f)\n",
               tc, total_ops, wall_ms, throughput, speedup);

        free(threads);
        free(args);
    }
}

/* ─── B3: Blok boyutu etkisi ────────────────────────────────── */
static void bench_block_sizes(void)
{
    struct { size_t size; const char *label; } sizes[] = {
        {8,    "8B (kucuk)"},
        {64,   "64B"},
        {256,  "256B"},
        {1024, "1KB"},
        {4096, "4KB"},
        {0, NULL}
    };
    int iters = 5000;

    printf(C_BLD C_CYN "\nB3: Blok Boyutu Etkisi (%d islem)\n" C_RST, iters);
    printf("  %-14s  %-10s  %-10s  %-10s\n",
           "Boyut", "Alloc(ms)", "Free(ms)", "Toplam(ms)");
    printf("  %s\n", "--------------------------------------------");

    for (int s = 0; sizes[s].label; s++) {
        allocator_destroy();
        allocator_init(STRATEGY_BEST_FIT);

        void **ptrs = malloc(sizeof(void *) * iters);
        struct timespec t0, t1;

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < iters; i++) ptrs[i] = my_malloc(sizes[s].size);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double alloc_ms = elapsed_ms(t0, t1);

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < iters; i++) my_free(ptrs[i]);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double free_ms = elapsed_ms(t0, t1);

        printf("  %-14s  %-10.3f  %-10.3f  %-10.3f\n",
               sizes[s].label, alloc_ms, free_ms, alloc_ms + free_ms);

        free(ptrs);
    }
}

/* ─── B4: Fragmentation yük altında ────────────────────────── */
static void bench_fragmentation_under_load(void)
{
    printf(C_BLD C_CYN "\nB4: Fragmentation Yuk Altinda\n" C_RST);

    alloc_strategy_t strats[] = {STRATEGY_FIRST_FIT, STRATEGY_BEST_FIT};
    const char *names[]       = {"First-Fit", "Best-Fit "};

    for (int s = 0; s < 2; s++) {
        allocator_destroy();
        allocator_init(strats[s]);

        void *ptrs[256];
        /* Değişen boyutlarda tahsis */
        for (int i = 0; i < 256; i++)
            ptrs[i] = my_malloc(16 + (i % 7) * 37 + (i % 3) * 100);

        /* Yarısını serbest bırak */
        for (int i = 0; i < 256; i += 2)
            my_free(ptrs[i]);

        double frag = allocator_fragmentation();
        printf("  %-10s fragmentation: %.2f%%\n", names[s], frag * 100.0);

        /* Kalanları temizle */
        for (int i = 1; i < 256; i += 2)
            my_free(ptrs[i]);
    }
}

/* ─── Ana fonksiyon ─────────────────────────────────────────── */
int main(void)
{
    srand(42);

    printf(C_BLD "\n============================================\n");
    printf("   CUSTOM ALLOCATOR PERFORMANS RAPORU\n");
    printf("============================================\n\n" C_RST);

    log_init("benchmark.log");

    /* B1 */
    printf(C_BLD C_CYN "B1: First-Fit vs Best-Fit (10000 islem)\n" C_RST);
    double ff_ms, bf_ms;
    bench_strategy(STRATEGY_FIRST_FIT, "First-Fit", 10000, &ff_ms);
    bench_strategy(STRATEGY_BEST_FIT,  "Best-Fit",  10000, &bf_ms);
    printf("  Fark: %.2f ms (%s daha hizli)\n",
           (ff_ms > bf_ms) ? ff_ms - bf_ms : bf_ms - ff_ms,
           (ff_ms < bf_ms) ? "First-Fit" : "Best-Fit");

    /* B2 */
    bench_scalability();

    /* B3 */
    bench_block_sizes();

    /* B4 */
    bench_fragmentation_under_load();

    printf(C_BLD "\n============================================\n");
    printf("   PERFORMANS DEGERLENDIRMESI TAMAMLANDI\n");
    printf("============================================\n\n" C_RST);

    allocator_destroy();
    log_close();
    return 0;
}
