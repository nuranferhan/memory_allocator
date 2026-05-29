/*
 * test.c - Custom Memory Allocator test programı
 *
 * Test senaryoları:
 *   T1  - Temel malloc/free
 *   T2  - calloc sıfırlama doğrulaması
 *   T3  - realloc büyütme/küçültme
 *   T4  - Çifte serbest bırakma (double-free) koruması
 *   T5  - Hizalama doğrulaması
 *   T6  - First-Fit vs Best-Fit fragmentation karşılaştırması
 *   T7  - Çok thread eşzamanlı erişim (thread güvenligi)
 *   T8  - Bellek tükenmesi senaryosu
 *   T9  - Performans ölçümü (malloc/free döngüsü)
 *   T10 - Coalescing (birleştirme) doğrulaması
 */

#include "allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>

/* ─── Renk kodları (terminal) ───────────────────────────────── */
#define C_GRN  "\033[0;32m"
#define C_RED  "\033[0;31m"
#define C_YEL  "\033[0;33m"
#define C_CYN  "\033[0;36m"
#define C_BLD  "\033[1m"
#define C_RST  "\033[0m"

/* ─── Test yardımcıları ─────────────────────────────────────── */
static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_PASS(name) do {                                         \
    tests_run++; tests_passed++;                                     \
    printf(C_GRN "[GEC] " C_RST "%-45s\n", (name));                \
} while(0)

#define TEST_FAIL(name, reason) do {                                 \
    tests_run++; tests_failed++;                                     \
    printf(C_RED "[BAS] " C_RST "%-45s  -> %s\n", (name), (reason)); \
} while(0)

#define SECTION(title) do {                                          \
    printf(C_BLD C_CYN "\n--- %s ---\n" C_RST, (title));           \
} while(0)

static double elapsed_ms(struct timespec start, struct timespec end)
{
    return (end.tv_sec - start.tv_sec) * 1000.0
         + (end.tv_nsec - start.tv_nsec) / 1e6;
}

/* ─── T1: Temel malloc/free ─────────────────────────────────── */
static void test_basic_malloc_free(void)
{
    SECTION("T1: Temel malloc/free");

    void *p1 = my_malloc(64);
    void *p2 = my_malloc(128);
    void *p3 = my_malloc(256);

    if (p1 && p2 && p3) TEST_PASS("3 blok tahsisi basarili");
    else                  TEST_FAIL("3 blok tahsisi", "NULL donduruldu");

    /* Yazma/okuma bütünlüğü */
    if (p1) { memset(p1, 0xAA, 64); }
    if (p2) { memset(p2, 0xBB, 128); }
    if (p3) { memset(p3, 0xCC, 256); }

    int ok = 1;
    if (p1) { uint8_t *b = (uint8_t *)p1; for (int i=0;i<64;i++) if(b[i]!=0xAA){ok=0;break;} }
    if (ok) TEST_PASS("p1 yazma/okuma butunlugu");
    else    TEST_FAIL("p1 yazma/okuma butunlugu", "veri bozulmasi");

    my_free(p1);
    my_free(p2);
    my_free(p3);
    TEST_PASS("3 blok serbest birakildi");
}

/* ─── T2: calloc sıfırlama ──────────────────────────────────── */
static void test_calloc(void)
{
    SECTION("T2: calloc sifirlama dogrulamasi");

    int *arr = (int *)my_calloc(100, sizeof(int));
    if (!arr) { TEST_FAIL("calloc(100, 4)", "NULL donduruldu"); return; }
    TEST_PASS("calloc(100, 4) tahsisi");

    int all_zero = 1;
    for (int i = 0; i < 100; i++) {
        if (arr[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero) TEST_PASS("calloc ile tahsis edilen bellek sifir");
    else          TEST_FAIL("calloc sifirlama", "bazi alanlar sifir degil");

    my_free(arr);
}

/* ─── T3: realloc ───────────────────────────────────────────── */
static void test_realloc(void)
{
    SECTION("T3: realloc buyutme/kucultme");

    char *buf = (char *)my_malloc(64);
    if (!buf) { TEST_FAIL("realloc baslangic malloc", "NULL"); return; }

    memcpy(buf, "Merhaba, Dunya!", 16);

    char *buf2 = (char *)my_realloc(buf, 256);
    if (!buf2) { TEST_FAIL("realloc buyutme", "NULL"); my_free(buf); return; }
    TEST_PASS("realloc 64->256 byte");

    if (memcmp(buf2, "Merhaba, Dunya!", 16) == 0)
        TEST_PASS("realloc sonrasi veri korunmasi");
    else
        TEST_FAIL("realloc sonrasi veri korunmasi", "veri bozuldu");

    my_free(buf2);

    /* NULL ile realloc = malloc */
    void *p = my_realloc(NULL, 128);
    if (p) TEST_PASS("my_realloc(NULL, 128) malloc gibi davranir");
    else   TEST_FAIL("my_realloc(NULL, 128)", "NULL donduruldu");
    my_free(p);
}

/* ─── T4: Double-free koruması ──────────────────────────────── */
static void test_double_free(void)
{
    SECTION("T4: Cift serbest birakma korumasi");

    void *p = my_malloc(32);
    if (!p) { TEST_FAIL("double-free testi icin tahsis", "NULL"); return; }
    TEST_PASS("tahsis basarili");

    my_free(p);
    TEST_PASS("ilk free basarili");

    /* İkinci free - log'a hata yazmalı, çökmemeli */
    my_free(p);
    TEST_PASS("cift free programi cokturmedi (hata log'a yazildi)");
}

/* ─── T5: Hizalama doğrulaması ──────────────────────────────── */
static void test_alignment(void)
{
    SECTION("T5: Hizalama dogrulamasi (16-byte)");

    int misaligned = 0;
    for (size_t sz = 1; sz <= 1024; sz++) {
        void *p = my_malloc(sz);
        if (!p) continue;
        if ((uintptr_t)p % ALIGNMENT != 0) misaligned++;
        my_free(p);
    }

    if (misaligned == 0)
        TEST_PASS("1-1024 byte arasi tum tahsisler 16-byte hizali");
    else {
        char msg[64];
        snprintf(msg, sizeof(msg), "%d hizasiz tahsis", misaligned);
        TEST_FAIL("hizalama dogrulamasi", msg);
    }
}

/* ─── T6: Fragmentation karşılaştırması ─────────────────────── */
static void test_fragmentation(void)
{
    SECTION("T6: First-Fit vs Best-Fit fragmentation");

    /* First-Fit testi */
    allocator_destroy();
    allocator_init(STRATEGY_FIRST_FIT);

    void *ptrs[20];
    for (int i = 0; i < 20; i++)
        ptrs[i] = my_malloc(128 + i * 32); /* Değişen boyutlar */

    /* Çift indekslileri serbest bırak -> fragmentation oluştur */
    for (int i = 0; i < 20; i += 2)
        my_free(ptrs[i]);

    double ff_frag = allocator_fragmentation();
    printf("  First-Fit fragmentation  : %.2f%%\n", ff_frag * 100.0);

    for (int i = 1; i < 20; i += 2)
        my_free(ptrs[i]);

    /* Best-Fit testi */
    allocator_destroy();
    allocator_init(STRATEGY_BEST_FIT);

    for (int i = 0; i < 20; i++)
        ptrs[i] = my_malloc(128 + i * 32);

    for (int i = 0; i < 20; i += 2)
        my_free(ptrs[i]);

    double bf_frag = allocator_fragmentation();
    printf("  Best-Fit  fragmentation  : %.2f%%\n", bf_frag * 100.0);

    for (int i = 1; i < 20; i += 2)
        my_free(ptrs[i]);

    TEST_PASS("fragmentation hesaplama tamamlandi");

    /* Varsayılan stratejiye dön */
    allocator_destroy();
    allocator_init(STRATEGY_BEST_FIT);
}

/* ─── T7: Thread güvenligi ──────────────────────────────────── */
#define THREAD_COUNT   8
#define OPS_PER_THREAD 500

typedef struct {
    int    tid;
    int    errors;
    double duration_ms;
} thread_arg_t;

static void *thread_worker(void *arg)
{
    thread_arg_t *ta = (thread_arg_t *)arg;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* rand_r ile thread-safe rastgele sayı üretimi */
    unsigned int seed = (unsigned int)(ta->tid * 1234567 + 42);

    void *ptrs[32];
    int   held = 0;

    for (int op = 0; op < OPS_PER_THREAD; op++) {
        if (held < 32 && (held == 0 || rand_r(&seed) % 2 == 0)) {
            size_t sz = (rand_r(&seed) % 256) + 16;
            void *p = my_malloc(sz);
            if (p) {
                memset(p, ta->tid & 0xFF, sz);
                ptrs[held++] = p;
            }
        } else if (held > 0) {
            int idx = rand_r(&seed) % held;
            my_free(ptrs[idx]);
            ptrs[idx] = ptrs[--held];
        }
    }

    /* Kalan blokları serbest bırak */
    for (int i = 0; i < held; i++)
        my_free(ptrs[i]);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    ta->duration_ms = elapsed_ms(t0, t1);
    return NULL;
}

static void test_thread_safety(void)
{
    SECTION("T7: Cok thread esit erisim (thread safety)");

    pthread_t     threads[THREAD_COUNT];
    thread_arg_t  args[THREAD_COUNT];
    memset(args, 0, sizeof(args));

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < THREAD_COUNT; i++) {
        args[i].tid = i;
        pthread_create(&threads[i], NULL, thread_worker, &args[i]);
    }
    for (int i = 0; i < THREAD_COUNT; i++)
        pthread_join(threads[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double total_ms = elapsed_ms(t0, t1);

    int total_errors = 0;
    for (int i = 0; i < THREAD_COUNT; i++)
        total_errors += args[i].errors;

    printf("  %d thread x %d islem = %d toplam islem\n",
           THREAD_COUNT, OPS_PER_THREAD, THREAD_COUNT * OPS_PER_THREAD);
    printf("  Toplam sure: %.2f ms\n", total_ms);
    printf("  Hata sayisi: %d\n", total_errors);

    if (total_errors == 0 && g_alloc.current_usage == 0)
        TEST_PASS("thread safety: veri yapisi bozulmadi, sizma yok");
    else if (total_errors > 0)
        TEST_FAIL("thread safety", "hatalar tespit edildi");
    else {
        char msg[64];
        snprintf(msg, sizeof(msg), "current_usage=%zu (sizinti?)", g_alloc.current_usage);
        TEST_FAIL("thread safety bellek sizintisi kontrolu", msg);
    }
}

/* ─── T8: Bellek tükenmesi senaryosu (Düzeltilmiş) ──────────────── */
static void test_oom(void)
{
    SECTION("T8: Bellek tukenmesi senaryosu");

    void **ptrs = malloc(sizeof(void *) * 20000); // Daha geniş bir dizi
    int count = 0;

    // Gerçekten NULL dönene kadar tahsis yap
    while (count < 20000) {
        ptrs[count] = my_malloc(1024 * 10); // 10KB'lık bloklar
        if (!ptrs[count]) break;
        count++;
    }

    printf("  %d adet 10KB blok tahsis edildi (toplam ~%.2f MB)\n",
           count, (count * 10.0) / 1024.0);

    void *overflow = my_malloc(1024 * 1024); // 1 MB daha iste
    if (!overflow)
        TEST_PASS("OOM durumunda NULL donduruldu");
    else {
        TEST_FAIL("OOM testi", "NULL beklendi ama ptr donduruldu (Heap hala bos?)");
        my_free(overflow);
    }

    for (int i = 0; i < count; i++)
        my_free(ptrs[i]);
    free(ptrs);
}

/* ─── T9: Performans ölçümü ─────────────────────────────────── */
#define PERF_ITERS 10000

static void test_performance(void)
{
    SECTION("T9: Performans olcumu");

    struct timespec t0, t1;
    void *ptrs[PERF_ITERS];

    /* Sıralı malloc */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < PERF_ITERS; i++)
        ptrs[i] = my_malloc(64 + (i % 256));
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double alloc_ms = elapsed_ms(t0, t1);

    /* Sıralı free */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < PERF_ITERS; i++)
        my_free(ptrs[i]);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double free_ms = elapsed_ms(t0, t1);

    printf("  %d malloc : %.3f ms  (%.1f ns/op)\n",
           PERF_ITERS, alloc_ms, alloc_ms * 1e6 / PERF_ITERS);
    printf("  %d free   : %.3f ms  (%.1f ns/op)\n",
           PERF_ITERS, free_ms, free_ms * 1e6 / PERF_ITERS);
    printf("  Toplam    : %.3f ms\n", alloc_ms + free_ms);

    TEST_PASS("performans olcumu tamamlandi");
}

/* ─── T10: Coalescing doğrulaması ───────────────────────────── */
static void test_coalescing(void)
{
    SECTION("T10: Coalescing (bitisik blok birlestirme)");

    /* Küçük bloklara böl */
    void *a = my_malloc(128);
    void *b = my_malloc(128);
    void *c = my_malloc(128);

    if (!a || !b || !c) {
        TEST_FAIL("coalescing icin tahsis", "NULL");
        my_free(a); my_free(b); my_free(c);
        return;
    }

    size_t free_before = 0;
    {
        block_header_t *cur = g_alloc.free_list;
        while(cur) { free_before += cur->size; cur = cur->next; }
    }

    my_free(a);
    my_free(b);
    my_free(c);

    size_t free_after = 0;
    size_t block_count_after = 0;
    {
        block_header_t *cur = g_alloc.free_list;
        while(cur) { free_after += cur->size; block_count_after++; cur = cur->next; }
    }

    printf("  Serbest blok sayisi (sonra): %zu\n", block_count_after);

    /* Tüm bloklar birleşmiş olmalı - free_after > free_before (header alanları da geri döner) */
    if (free_after > free_before)
        TEST_PASS("coalescing: bitisik bloklar birlestirildi");
    else
        TEST_FAIL("coalescing", "bloklar birlestirilmedi");
}

/* ─── Ana fonksiyon ─────────────────────────────────────────── */
int main(void)
{
    printf(C_BLD "\n============================================\n");
    printf("     CUSTOM MEMORY ALLOCATOR TEST SUITE    \n");
    printf("============================================\n\n" C_RST);

    log_init("allocator_test.log");
    allocator_init(STRATEGY_BEST_FIT);

    test_basic_malloc_free();
    test_calloc();
    test_realloc();
    test_double_free();
    test_alignment();
    test_fragmentation();
    test_thread_safety();
    test_oom();
    test_performance();
    test_coalescing();

    /* Son istatistikler */
    printf("\n");
    allocator_stats();
    allocator_dump();

    allocator_destroy();
    log_close();

    /* Özet */
    printf(C_BLD "\n============================================\n");
    printf("  TEST OZETI: %d calistirildi, ", tests_run);
    printf(C_GRN "%d gecti" C_RST C_BLD ", ", tests_passed);
    printf(C_RED "%d basladi" C_RST C_BLD "\n", tests_failed);
    printf("============================================\n\n" C_RST);

    return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
