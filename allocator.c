/*
 * allocator.c - Kullanıcı alanında çalışan thread-safe özel bellek yöneticisi.
 */

#include "allocator.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

allocator_t g_alloc;

static _Alignas(ALIGNMENT) uint8_t static_heap[HEAP_SIZE];
static int allocator_ready = 0;

static inline block_header_t *ptr_to_header(void *ptr)
{
    return (block_header_t *)((uint8_t *)ptr - sizeof(block_header_t));
}

static inline void *header_to_ptr(block_header_t *hdr)
{
    return (void *)((uint8_t *)hdr + sizeof(block_header_t));
}

static int pointer_in_heap(const void *ptr)
{
    const uint8_t *p = (const uint8_t *)ptr;
    return p >= g_alloc.heap_start && p < g_alloc.heap_start + g_alloc.heap_size;
}

static int is_valid_header(block_header_t *hdr)
{
    if (!hdr || !pointer_in_heap(hdr)) return 0;
    if (hdr->magic != MAGIC_FREE && hdr->magic != MAGIC_ALLOC) return 0;
    if ((uint8_t *)hdr + sizeof(block_header_t) + hdr->size >
        g_alloc.heap_start + g_alloc.heap_size) return 0;
    return 1;
}

static void freelist_insert(block_header_t *blk)
{
    blk->is_free = 1;
    blk->magic = MAGIC_FREE;
    blk->prev = NULL;
    blk->next = g_alloc.free_list;

    if (g_alloc.free_list)
        g_alloc.free_list->prev = blk;
    g_alloc.free_list = blk;
}

static void freelist_remove(block_header_t *blk)
{
    if (blk->prev)
        blk->prev->next = blk->next;
    else
        g_alloc.free_list = blk->next;

    if (blk->next)
        blk->next->prev = blk->prev;

    blk->next = NULL;
    blk->prev = NULL;
}

static void split_block(block_header_t *blk, size_t size)
{
    if (blk->size < size + MIN_BLOCK_SIZE)
        return;

    block_header_t *new_blk =
        (block_header_t *)((uint8_t *)header_to_ptr(blk) + size);

    new_blk->size = blk->size - size - sizeof(block_header_t);
    new_blk->magic = MAGIC_FREE;
    new_blk->is_free = 1;
    new_blk->next_phys = blk->next_phys;
    new_blk->prev_phys = blk;
    new_blk->next = NULL;
    new_blk->prev = NULL;

    if (blk->next_phys)
        blk->next_phys->prev_phys = new_blk;

    blk->next_phys = new_blk;
    blk->size = size;

    freelist_insert(new_blk);
}

static block_header_t *coalesce(block_header_t *blk)
{
    block_header_t *next = blk->next_phys;
    if (next && next->is_free && is_valid_header(next)) {
        freelist_remove(next);
        blk->size += sizeof(block_header_t) + next->size;
        blk->next_phys = next->next_phys;
        if (next->next_phys)
            next->next_phys->prev_phys = blk;
        next->magic = 0;
    }

    block_header_t *prev = blk->prev_phys;
    if (prev && prev->is_free && is_valid_header(prev)) {
        freelist_remove(prev);
        freelist_remove(blk);
        prev->size += sizeof(block_header_t) + blk->size;
        prev->next_phys = blk->next_phys;
        if (blk->next_phys)
            blk->next_phys->prev_phys = prev;
        blk->magic = 0;
        blk = prev;
        freelist_insert(blk);
    }

    return blk;
}

static block_header_t *find_first_fit(size_t size)
{
    block_header_t *cur = g_alloc.free_list;
    while (cur) {
        if (cur->size >= size) return cur;
        cur = cur->next;
    }
    return NULL;
}

static block_header_t *find_best_fit(size_t size)
{
    block_header_t *cur = g_alloc.free_list;
    block_header_t *best = NULL;

    while (cur) {
        if (cur->size >= size && (!best || cur->size < best->size))
            best = cur;
        cur = cur->next;
    }

    return best;
}

const char *allocator_strategy_name(alloc_strategy_t strategy)
{
    return strategy == STRATEGY_FIRST_FIT ? "First-Fit" : "Best-Fit";
}

int allocator_init(alloc_strategy_t strategy)
{
    if (allocator_ready)
        allocator_destroy();

    if (pthread_mutex_init(&g_alloc.lock, NULL) != 0)
        return -1;

    pthread_mutex_lock(&g_alloc.lock);

    memset(static_heap, 0, sizeof(static_heap));
    g_alloc.heap_start = static_heap;
    g_alloc.heap_size = HEAP_SIZE;
    g_alloc.strategy = strategy;
    g_alloc.total_allocated = 0;
    g_alloc.total_freed = 0;
    g_alloc.current_usage = 0;
    g_alloc.alloc_count = 0;
    g_alloc.free_count = 0;
    g_alloc.peak_usage = 0;

    block_header_t *initial_blk = (block_header_t *)static_heap;
    initial_blk->size = HEAP_SIZE - sizeof(block_header_t);
    initial_blk->magic = MAGIC_FREE;
    initial_blk->is_free = 1;
    initial_blk->next = NULL;
    initial_blk->prev = NULL;
    initial_blk->next_phys = NULL;
    initial_blk->prev_phys = NULL;

    g_alloc.free_list = initial_blk;
    allocator_ready = 1;

    pthread_mutex_unlock(&g_alloc.lock);
    return 0;
}

void *my_malloc(size_t size)
{
    if (size == 0) return NULL;
    if (!allocator_ready && allocator_init(STRATEGY_BEST_FIT) != 0)
        return NULL;

    size_t aligned = ALIGN(size);
    if (aligned < size) {
        errno = ENOMEM;
        return NULL;
    }

    pthread_mutex_lock(&g_alloc.lock);

    block_header_t *blk = (g_alloc.strategy == STRATEGY_FIRST_FIT)
        ? find_first_fit(aligned)
        : find_best_fit(aligned);

    if (!blk) {
        pthread_mutex_unlock(&g_alloc.lock);
        errno = ENOMEM;
        log_write(LOG_WARN, "my_malloc(%zu): yeterli serbest blok yok", size);
        return NULL;
    }

    freelist_remove(blk);
    split_block(blk, aligned);

    blk->is_free = 0;
    blk->magic = MAGIC_ALLOC;
    g_alloc.current_usage += blk->size;
    g_alloc.total_allocated += blk->size;
    g_alloc.alloc_count++;
    if (g_alloc.current_usage > g_alloc.peak_usage)
        g_alloc.peak_usage = g_alloc.current_usage;

    pthread_mutex_unlock(&g_alloc.lock);
    return header_to_ptr(blk);
}

void my_free(void *ptr)
{
    if (!ptr) return;
    if (!allocator_ready) return;

    pthread_mutex_lock(&g_alloc.lock);

    if (!pointer_in_heap(ptr)) {
        pthread_mutex_unlock(&g_alloc.lock);
        log_write(LOG_ERROR, "my_free: heap dışında adres verildi (%p)", ptr);
        return;
    }

    block_header_t *blk = ptr_to_header(ptr);
    if (!is_valid_header(blk)) {
        pthread_mutex_unlock(&g_alloc.lock);
        log_write(LOG_ERROR, "my_free: geçersiz blok başlığı (%p)", ptr);
        return;
    }

    if (blk->is_free || blk->magic == MAGIC_FREE) {
        pthread_mutex_unlock(&g_alloc.lock);
        log_write(LOG_ERROR, "my_free: çifte serbest bırakma engellendi (%p)", ptr);
        return;
    }

    g_alloc.current_usage -= blk->size;
    g_alloc.total_freed += blk->size;
    g_alloc.free_count++;

    freelist_insert(blk);
    coalesce(blk);

    pthread_mutex_unlock(&g_alloc.lock);
}

void *my_calloc(size_t nmemb, size_t size)
{
    if (nmemb != 0 && size > (size_t)-1 / nmemb) {
        errno = ENOMEM;
        return NULL;
    }

    size_t total = nmemb * size;
    void *ptr = my_malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *my_realloc(void *ptr, size_t new_size)
{
    if (!ptr) return my_malloc(new_size);
    if (new_size == 0) {
        my_free(ptr);
        return NULL;
    }

    pthread_mutex_lock(&g_alloc.lock);
    block_header_t *blk = ptr_to_header(ptr);
    if (!is_valid_header(blk) || blk->is_free) {
        pthread_mutex_unlock(&g_alloc.lock);
        log_write(LOG_ERROR, "my_realloc: geçersiz adres (%p)", ptr);
        return NULL;
    }

    size_t old_size = blk->size;
    pthread_mutex_unlock(&g_alloc.lock);

    if (old_size >= ALIGN(new_size))
        return ptr;

    void *new_ptr = my_malloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size);
        my_free(ptr);
    }
    return new_ptr;
}

int allocator_get_snapshot(allocator_snapshot_t *out)
{
    if (!out || !allocator_ready) return -1;

    pthread_mutex_lock(&g_alloc.lock);

    memset(out, 0, sizeof(*out));
    out->heap_size = g_alloc.heap_size;
    out->current_usage = g_alloc.current_usage;
    out->peak_usage = g_alloc.peak_usage;
    out->total_allocated = g_alloc.total_allocated;
    out->total_freed = g_alloc.total_freed;
    out->alloc_count = g_alloc.alloc_count;
    out->free_count = g_alloc.free_count;
    out->strategy = g_alloc.strategy;

    for (block_header_t *cur = g_alloc.free_list; cur; cur = cur->next) {
        out->free_bytes += cur->size;
        out->free_block_count++;
        if (cur->size > out->largest_free_block)
            out->largest_free_block = cur->size;
    }

    for (block_header_t *cur = (block_header_t *)g_alloc.heap_start;
         cur && is_valid_header(cur);
         cur = cur->next_phys) {
        if (!cur->is_free)
            out->used_block_count++;
    }

    out->fragmentation = out->free_bytes > 0
        ? 1.0 - (double)out->largest_free_block / (double)out->free_bytes
        : 0.0;

    pthread_mutex_unlock(&g_alloc.lock);
    return 0;
}

void allocator_dump(void)
{
    pthread_mutex_lock(&g_alloc.lock);

    printf("\n--- HEAP DÖKÜMÜ ---\n");
    for (block_header_t *cur = (block_header_t *)g_alloc.heap_start;
         cur && is_valid_header(cur);
         cur = cur->next_phys) {
        printf("[%p] Boyut: %zu, Durum: %s\n",
               (void *)cur, cur->size, cur->is_free ? "SERBEST" : "DOLU");
    }

    pthread_mutex_unlock(&g_alloc.lock);
}

void allocator_stats(void)
{
    allocator_snapshot_t s;
    if (allocator_get_snapshot(&s) != 0) return;

    printf("\n--- İSTATİSTİKLER ---\n");
    printf("Strateji: %s\n", allocator_strategy_name(s.strategy));
    printf("Anlık kullanım: %zu byte, Zirve: %zu byte\n",
           s.current_usage, s.peak_usage);
    printf("Toplam malloc: %zu, Toplam free: %zu\n",
           s.alloc_count, s.free_count);
    printf("Fragmentation: %.2f%%\n", s.fragmentation * 100.0);
}

double allocator_fragmentation(void)
{
    allocator_snapshot_t s;
    if (allocator_get_snapshot(&s) != 0)
        return 0.0;
    return s.fragmentation;
}

void allocator_destroy(void)
{
    if (!allocator_ready)
        return;

    pthread_mutex_destroy(&g_alloc.lock);
    memset(&g_alloc, 0, sizeof(g_alloc));
    allocator_ready = 0;
}
