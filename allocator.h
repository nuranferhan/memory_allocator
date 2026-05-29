#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

/* Sabitler */
#define HEAP_SIZE          (16 * 1024 * 1024)   /* 16 MB statik heap */
#define ALIGNMENT          16                    /* 16-byte hizalama  */
#define ALIGN(size)        (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define MIN_BLOCK_SIZE     (sizeof(block_header_t) + ALIGNMENT)
#define MAGIC_FREE         0xDEADBEEF
#define MAGIC_ALLOC        0xCAFEBABE

typedef enum {
    STRATEGY_FIRST_FIT = 0,
    STRATEGY_BEST_FIT  = 1
} alloc_strategy_t;

typedef struct block_header {
    size_t              size;       /* Kullanıcıya dönen net boyut */
    uint32_t            magic;      /* Bütünlük sihirli sayısı     */
    int                 is_free;    /* 1 = serbest, 0 = dolu       */
    struct block_header *next;      /* Serbest liste sonraki       */
    struct block_header *prev;      /* Serbest liste önceki        */
    struct block_header *next_phys; /* Fiziksel sonraki blok       */
    struct block_header *prev_phys; /* Fiziksel önceki blok        */
} block_header_t;

typedef struct {
    uint8_t            *heap_start;
    size_t              heap_size;
    block_header_t     *free_list;
    alloc_strategy_t    strategy;
    pthread_mutex_t     lock;

    size_t  total_allocated;
    size_t  total_freed;
    size_t  current_usage;
    size_t  alloc_count;
    size_t  free_count;
    size_t  peak_usage;
} allocator_t;

typedef struct {
    size_t heap_size;
    size_t current_usage;
    size_t peak_usage;
    size_t total_allocated;
    size_t total_freed;
    size_t alloc_count;
    size_t free_count;
    size_t free_bytes;
    size_t largest_free_block;
    size_t free_block_count;
    size_t used_block_count;
    double fragmentation;
    alloc_strategy_t strategy;
} allocator_snapshot_t;

int    allocator_init(alloc_strategy_t strategy);
void   allocator_destroy(void);

void  *my_malloc(size_t size);
void   my_free(void *ptr);
void  *my_calloc(size_t nmemb, size_t size);
void  *my_realloc(void *ptr, size_t new_size);

void   allocator_dump(void);
void   allocator_stats(void);
double allocator_fragmentation(void);
int    allocator_get_snapshot(allocator_snapshot_t *out);
const char *allocator_strategy_name(alloc_strategy_t strategy);

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

int  log_init(const char *path);
void log_close(void);
void log_write(log_level_t level, const char *fmt, ...);

extern allocator_t g_alloc;

#endif /* ALLOCATOR_H */
