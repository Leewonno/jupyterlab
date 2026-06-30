#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>

typedef int    cudaError_t;
typedef void*  cudaStream_t;
typedef void*  cudaMemPool_t;

#define cudaSuccess               0
#define cudaErrorMemoryAllocation 2
#define cudaErrorNoDevice         100

#define MAX_MEMORY_BYTES  (${LIMIT_BYTES}ULL)
#define LOCK_FILE         "/dev/shm/gpu_single.lock"

/* ★ 핵심: ifndef로 감싸서 -D 옵션이 항상 우선 */
#ifndef REAL_LIBCUDART
#error "REAL_LIBCUDART must be defined: gcc ... -DREAL_LIBCUDART=\"/path/to/libcudart.so.12.real\""
#endif

static void           *g_real_lib      = NULL;
static pthread_once_t  g_init_once     = PTHREAD_ONCE_INIT;
static int             g_lock_fd       = -1;
static int             g_lock_acquired = 0;
static size_t          g_allocated     = 0;
static pthread_mutex_t g_mem_lock      = PTHREAD_MUTEX_INITIALIZER;

/* ===== 할당 추적 테이블 (devPtr -> size) =====
 * 모든 접근은 반드시 g_mem_lock 을 잡은 상태에서만 수행한다.
 * 체이닝 해시맵. 노드는 libc malloc/free 로 관리(CUDA 재진입 없음). */
#define ALLOC_HASH_BUCKETS 8192u   /* 반드시 2의 거듭제곱 */

typedef struct alloc_node {
    void              *ptr;
    size_t             size;
    struct alloc_node *next;
} alloc_node_t;

static alloc_node_t *g_alloc_table[ALLOC_HASH_BUCKETS]; /* 정적 → 0 초기화 */

static inline size_t _hash_ptr(const void *p) {
    /* fibonacci/murmur 식 믹싱 후 버킷 마스크 */
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)(x & (ALLOC_HASH_BUCKETS - 1u));
}

/* g_mem_lock 보유 상태에서 호출. 성공한 할당만 등록. */
static void _track_insert(void *ptr, size_t size) {
    if (!ptr) return;
    alloc_node_t *n = (alloc_node_t*)malloc(sizeof(*n));
    if (!n) {
        /* 노드 할당 실패: 추적 누락 → 나중에 free 시 차감 안 됨(누적 과대).
         * 크래시는 피하고 경고만 남긴다. */
        fprintf(stderr, "[GPU-SLICER] WARN: 추적 노드 malloc 실패, %zuMB 미추적\n", size>>20);
        return;
    }
    size_t b = _hash_ptr(ptr);
    n->ptr  = ptr;
    n->size = size;
    n->next = g_alloc_table[b];
    g_alloc_table[b] = n;
}

/* g_mem_lock 보유 상태에서 호출. 등록돼 있으면 제거하고 size 반환, 없으면 0. */
static size_t _track_remove(void *ptr) {
    if (!ptr) return 0;
    size_t b = _hash_ptr(ptr);
    alloc_node_t **pp = &g_alloc_table[b];
    while (*pp) {
        if ((*pp)->ptr == ptr) {
            alloc_node_t *victim = *pp;
            size_t sz = victim->size;
            *pp = victim->next;
            free(victim);
            return sz;
        }
        pp = &(*pp)->next;
    }
    return 0; /* 미추적 포인터(shim 로드 전 할당, 미후킹 경로, NULL 등) */
}

/* g_mem_lock 보유 상태에서 호출. 누적 카운터 차감(언더플로 가드). */
static void _account_free(void *ptr) {
    size_t sz = _track_remove(ptr);
    if (sz == 0) return;
    g_allocated = (g_allocated >= sz) ? (g_allocated - sz) : 0;
    fprintf(stderr, "[GPU-SLICER] 해제 %zuMB (누적 %zuMB/${LIMIT_GB}GB)\n",
            sz >> 20, g_allocated >> 20);
}

static void _load_real(void) {
    g_real_lib = dlopen(REAL_LIBCUDART, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!g_real_lib) {
        fprintf(stderr, "[GPU-SLICER] FATAL: %s\n", dlerror());
        _exit(1);
    }
    fprintf(stderr, "[GPU-SLICER] cudart shim 활성화 (PID=%d)\n", getpid());
}

static void *_sym(const char *name) {
    pthread_once(&g_init_once, _load_real);
    return dlsym(g_real_lib, name);
}

static cudaError_t _enforce_single(void) {
    if (g_lock_acquired) return cudaSuccess;
    g_lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (g_lock_fd < 0) {
        fprintf(stderr, "[GPU-SLICER] lock 파일 생성 실패\n");
        return cudaErrorNoDevice;
    }
    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr,
            "\n================================================\n"
            "[GPU-SLICER] GPU 사용 불가\n"
            "이 컨테이너에서 이미 다른 커널이 GPU를 점유 중입니다.\n"
            "다른 노트북 커널을 종료 후 다시 시도하세요.\n"
            "================================================\n\n");
        close(g_lock_fd);
        g_lock_fd = -1;
        return cudaErrorNoDevice;
    }
    g_lock_acquired = 1;
    fprintf(stderr, "[GPU-SLICER] GPU 허가 (PID=%d, 한도=${LIMIT_GB}GB)\n", getpid());
    return cudaSuccess;
}

static cudaError_t _alloc_check(size_t size) {
    if (g_allocated + size > MAX_MEMORY_BYTES) {
        fprintf(stderr,
            "[GPU-SLICER] OOM: 요청=%zuMB 누적=%zuMB 한도=${LIMIT_GB}GB\n",
            size >> 20, g_allocated >> 20);
        return cudaErrorMemoryAllocation;
    }
    return cudaSuccess;
}

/* ===== 핵심 후킹 ===== */

cudaError_t cudaGetDeviceCount(int *count) {
    typedef cudaError_t fn_t(int*);
    cudaError_t r = _enforce_single();
    if (r != cudaSuccess) { *count = 0; return r; }
    return ((fn_t*)_sym("cudaGetDeviceCount"))(count);
}

cudaError_t cudaMalloc(void **devPtr, size_t size) {
    typedef cudaError_t fn_t(void**, size_t);
    pthread_mutex_lock(&g_mem_lock);
    cudaError_t r = _alloc_check(size);
    if (r != cudaSuccess) { pthread_mutex_unlock(&g_mem_lock); return r; }
    r = ((fn_t*)_sym("cudaMalloc"))(devPtr, size);
    if (r == cudaSuccess) {
        g_allocated += size;
        _track_insert(*devPtr, size);
        fprintf(stderr, "[GPU-SLICER] 할당 %zuMB (누적 %zuMB/${LIMIT_GB}GB)\n",
            size>>20, g_allocated>>20);
    }
    pthread_mutex_unlock(&g_mem_lock);
    return r;
}

cudaError_t cudaMallocManaged(void **devPtr, size_t size, unsigned int flags) {
    typedef cudaError_t fn_t(void**, size_t, unsigned int);
    pthread_mutex_lock(&g_mem_lock);
    cudaError_t r = _alloc_check(size);
    if (r != cudaSuccess) { pthread_mutex_unlock(&g_mem_lock); return r; }
    r = ((fn_t*)_sym("cudaMallocManaged"))(devPtr, size, flags);
    if (r == cudaSuccess) {
        g_allocated += size;
        _track_insert(*devPtr, size);
    }
    pthread_mutex_unlock(&g_mem_lock);
    return r;
}

cudaError_t cudaMallocAsync(void **devPtr, size_t size, cudaStream_t stream) {
    typedef cudaError_t fn_t(void**, size_t, cudaStream_t);
    pthread_mutex_lock(&g_mem_lock);
    cudaError_t r = _alloc_check(size);
    if (r != cudaSuccess) { pthread_mutex_unlock(&g_mem_lock); return r; }
    r = ((fn_t*)_sym("cudaMallocAsync"))(devPtr, size, stream);
    if (r == cudaSuccess) {
        g_allocated += size;
        _track_insert(*devPtr, size);
    }
    pthread_mutex_unlock(&g_mem_lock);
    return r;
}

cudaError_t cudaMallocFromPoolAsync(void **devPtr, size_t size,
                                    cudaMemPool_t pool, cudaStream_t stream) {
    typedef cudaError_t fn_t(void**, size_t, cudaMemPool_t, cudaStream_t);
    pthread_mutex_lock(&g_mem_lock);
    cudaError_t r = _alloc_check(size);
    if (r != cudaSuccess) { pthread_mutex_unlock(&g_mem_lock); return r; }
    r = ((fn_t*)_sym("cudaMallocFromPoolAsync"))(devPtr, size, pool, stream);
    if (r == cudaSuccess) {
        g_allocated += size;
        _track_insert(*devPtr, size);
    }
    pthread_mutex_unlock(&g_mem_lock);
    return r;
}

cudaError_t cudaFree(void *devPtr) {
    typedef cudaError_t fn_t(void*);
    pthread_mutex_lock(&g_mem_lock);
    cudaError_t r = ((fn_t*)_sym("cudaFree"))(devPtr);
    if (r == cudaSuccess) {
        _account_free(devPtr);   /* 추적된 경우에만 차감 */
    }
    pthread_mutex_unlock(&g_mem_lock);
    return r;
}

cudaError_t cudaFreeAsync(void *devPtr, cudaStream_t stream) {
    typedef cudaError_t fn_t(void*, cudaStream_t);
    pthread_mutex_lock(&g_mem_lock);
    cudaError_t r = ((fn_t*)_sym("cudaFreeAsync"))(devPtr, stream);
    if (r == cudaSuccess) {
        /* 주의: 실제 반환은 스트림 완료 시점이지만 회계상 즉시 차감 */
        _account_free(devPtr);
    }
    pthread_mutex_unlock(&g_mem_lock);
    return r;
}

cudaError_t cudaMemGetInfo(size_t *free, size_t *total) {
    typedef cudaError_t fn_t(size_t*, size_t*);
    cudaError_t r = ((fn_t*)_sym("cudaMemGetInfo"))(free, total);
    if (r == cudaSuccess) {
        *total = MAX_MEMORY_BYTES;
        *free  = (g_allocated < MAX_MEMORY_BYTES) ?
                 (MAX_MEMORY_BYTES - g_allocated) : 0;
    }
    return r;
}

__attribute__((constructor))
static void _shim_ctor(void) {
    pthread_once(&g_init_once, _load_real);
}
