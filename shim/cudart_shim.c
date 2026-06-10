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
#define MAX_MEMORY_BYTES          (4294967296ULL)
#define LOCK_FILE                 "/dev/shm/gpu_single.lock"

#ifndef REAL_LIBCUDART
#error "REAL_LIBCUDART must be defined via -D flag"
#endif

static void           *g_real_lib      = NULL;
static pthread_once_t  g_init_once     = PTHREAD_ONCE_INIT;
static int             g_lock_fd       = -1;
static int             g_lock_acquired = 0;
static size_t          g_allocated     = 0;
static pthread_mutex_t g_mem_lock      = PTHREAD_MUTEX_INITIALIZER;

static void _load_real(void) {
    g_real_lib = dlopen(REAL_LIBCUDART, RTLD_NOW | RTLD_DEEPBIND | RTLD_NODELETE);
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
        return cudaErrorNoDevice;
    }
    g_lock_acquired = 1;
    fprintf(stderr, "[GPU-SLICER] GPU 허가 (PID=%d, 한도=4GB)\n", getpid());
    return cudaSuccess;
}

static cudaError_t _alloc_check(size_t size) {
    if (g_allocated + size > MAX_MEMORY_BYTES) {
        fprintf(stderr,
            "[GPU-SLICER] OOM: 요청=%zuMB 누적=%zuMB 한도=4GB\n",
            size>>20, g_allocated>>20);
        return cudaErrorMemoryAllocation;
    }
    return cudaSuccess;
}

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
        fprintf(stderr, "[GPU-SLICER] 할당 %zuMB (누적 %zuMB/4GB)\n",
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
    if (r == cudaSuccess) { g_allocated += size; }
    pthread_mutex_unlock(&g_mem_lock);
    return r;
}

cudaError_t cudaMallocAsync(void **devPtr, size_t size, cudaStream_t stream) {
    typedef cudaError_t fn_t(void**, size_t, cudaStream_t);
    pthread_mutex_lock(&g_mem_lock);
    cudaError_t r = _alloc_check(size);
    if (r != cudaSuccess) { pthread_mutex_unlock(&g_mem_lock); return r; }
    r = ((fn_t*)_sym("cudaMallocAsync"))(devPtr, size, stream);
    if (r == cudaSuccess) { g_allocated += size; }
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
    if (r == cudaSuccess) { g_allocated += size; }
    pthread_mutex_unlock(&g_mem_lock);
    return r;
}

cudaError_t cudaFree(void *devPtr) {
    typedef cudaError_t fn_t(void*);
    return ((fn_t*)_sym("cudaFree"))(devPtr);
}

cudaError_t cudaFreeAsync(void *devPtr, cudaStream_t stream) {
    typedef cudaError_t fn_t(void*, cudaStream_t);
    return ((fn_t*)_sym("cudaFreeAsync"))(devPtr, stream);
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

void* cudaArrayGetInfo() __attribute__((ifunc("cudaArrayGetInfo__res")));
static void* cudaArrayGetInfo__res(void) { return _sym("cudaArrayGetInfo"); }
void* cudaArrayGetMemoryRequirements() __attribute__((ifunc("cudaArrayGetMemoryRequirements__res")));
static void* cudaArrayGetMemoryRequirements__res(void) { return _sym("cudaArrayGetMemoryRequirements"); }
void* cudaArrayGetPlane() __attribute__((ifunc("cudaArrayGetPlane__res")));
static void* cudaArrayGetPlane__res(void) { return _sym("cudaArrayGetPlane"); }
void* cudaArrayGetSparseProperties() __attribute__((ifunc("cudaArrayGetSparseProperties__res")));
static void* cudaArrayGetSparseProperties__res(void) { return _sym("cudaArrayGetSparseProperties"); }
void* cudaChooseDevice() __attribute__((ifunc("cudaChooseDevice__res")));
static void* cudaChooseDevice__res(void) { return _sym("cudaChooseDevice"); }
void* cudaCreateChannelDesc() __attribute__((ifunc("cudaCreateChannelDesc__res")));
static void* cudaCreateChannelDesc__res(void) { return _sym("cudaCreateChannelDesc"); }
void* cudaCreateSurfaceObject() __attribute__((ifunc("cudaCreateSurfaceObject__res")));
static void* cudaCreateSurfaceObject__res(void) { return _sym("cudaCreateSurfaceObject"); }
void* cudaCreateTextureObject() __attribute__((ifunc("cudaCreateTextureObject__res")));
static void* cudaCreateTextureObject__res(void) { return _sym("cudaCreateTextureObject"); }
void* cudaCtxResetPersistingL2Cache() __attribute__((ifunc("cudaCtxResetPersistingL2Cache__res")));
static void* cudaCtxResetPersistingL2Cache__res(void) { return _sym("cudaCtxResetPersistingL2Cache"); }
void* cudaDestroyExternalMemory() __attribute__((ifunc("cudaDestroyExternalMemory__res")));
static void* cudaDestroyExternalMemory__res(void) { return _sym("cudaDestroyExternalMemory"); }
void* cudaDestroyExternalSemaphore() __attribute__((ifunc("cudaDestroyExternalSemaphore__res")));
static void* cudaDestroyExternalSemaphore__res(void) { return _sym("cudaDestroyExternalSemaphore"); }
void* cudaDestroySurfaceObject() __attribute__((ifunc("cudaDestroySurfaceObject__res")));
static void* cudaDestroySurfaceObject__res(void) { return _sym("cudaDestroySurfaceObject"); }
void* cudaDestroyTextureObject() __attribute__((ifunc("cudaDestroyTextureObject__res")));
static void* cudaDestroyTextureObject__res(void) { return _sym("cudaDestroyTextureObject"); }
void* cudaDeviceCanAccessPeer() __attribute__((ifunc("cudaDeviceCanAccessPeer__res")));
static void* cudaDeviceCanAccessPeer__res(void) { return _sym("cudaDeviceCanAccessPeer"); }
void* cudaDeviceDisablePeerAccess() __attribute__((ifunc("cudaDeviceDisablePeerAccess__res")));
static void* cudaDeviceDisablePeerAccess__res(void) { return _sym("cudaDeviceDisablePeerAccess"); }
void* cudaDeviceEnablePeerAccess() __attribute__((ifunc("cudaDeviceEnablePeerAccess__res")));
static void* cudaDeviceEnablePeerAccess__res(void) { return _sym("cudaDeviceEnablePeerAccess"); }
void* cudaDeviceFlushGPUDirectRDMAWrites() __attribute__((ifunc("cudaDeviceFlushGPUDirectRDMAWrites__res")));
static void* cudaDeviceFlushGPUDirectRDMAWrites__res(void) { return _sym("cudaDeviceFlushGPUDirectRDMAWrites"); }
void* cudaDeviceGetAttribute() __attribute__((ifunc("cudaDeviceGetAttribute__res")));
static void* cudaDeviceGetAttribute__res(void) { return _sym("cudaDeviceGetAttribute"); }
void* cudaDeviceGetByPCIBusId() __attribute__((ifunc("cudaDeviceGetByPCIBusId__res")));
static void* cudaDeviceGetByPCIBusId__res(void) { return _sym("cudaDeviceGetByPCIBusId"); }
void* cudaDeviceGetCacheConfig() __attribute__((ifunc("cudaDeviceGetCacheConfig__res")));
static void* cudaDeviceGetCacheConfig__res(void) { return _sym("cudaDeviceGetCacheConfig"); }
void* cudaDeviceGetDefaultMemPool() __attribute__((ifunc("cudaDeviceGetDefaultMemPool__res")));
static void* cudaDeviceGetDefaultMemPool__res(void) { return _sym("cudaDeviceGetDefaultMemPool"); }
void* cudaDeviceGetGraphMemAttribute() __attribute__((ifunc("cudaDeviceGetGraphMemAttribute__res")));
static void* cudaDeviceGetGraphMemAttribute__res(void) { return _sym("cudaDeviceGetGraphMemAttribute"); }
void* cudaDeviceGetLimit() __attribute__((ifunc("cudaDeviceGetLimit__res")));
static void* cudaDeviceGetLimit__res(void) { return _sym("cudaDeviceGetLimit"); }
void* cudaDeviceGetMemPool() __attribute__((ifunc("cudaDeviceGetMemPool__res")));
static void* cudaDeviceGetMemPool__res(void) { return _sym("cudaDeviceGetMemPool"); }
void* cudaDeviceGetNvSciSyncAttributes() __attribute__((ifunc("cudaDeviceGetNvSciSyncAttributes__res")));
static void* cudaDeviceGetNvSciSyncAttributes__res(void) { return _sym("cudaDeviceGetNvSciSyncAttributes"); }
void* cudaDeviceGetP2PAttribute() __attribute__((ifunc("cudaDeviceGetP2PAttribute__res")));
static void* cudaDeviceGetP2PAttribute__res(void) { return _sym("cudaDeviceGetP2PAttribute"); }
void* cudaDeviceGetPCIBusId() __attribute__((ifunc("cudaDeviceGetPCIBusId__res")));
static void* cudaDeviceGetPCIBusId__res(void) { return _sym("cudaDeviceGetPCIBusId"); }
void* cudaDeviceGetSharedMemConfig() __attribute__((ifunc("cudaDeviceGetSharedMemConfig__res")));
static void* cudaDeviceGetSharedMemConfig__res(void) { return _sym("cudaDeviceGetSharedMemConfig"); }
void* cudaDeviceGetStreamPriorityRange() __attribute__((ifunc("cudaDeviceGetStreamPriorityRange__res")));
static void* cudaDeviceGetStreamPriorityRange__res(void) { return _sym("cudaDeviceGetStreamPriorityRange"); }
void* cudaDeviceGetTexture1DLinearMaxWidth() __attribute__((ifunc("cudaDeviceGetTexture1DLinearMaxWidth__res")));
static void* cudaDeviceGetTexture1DLinearMaxWidth__res(void) { return _sym("cudaDeviceGetTexture1DLinearMaxWidth"); }
void* cudaDeviceGraphMemTrim() __attribute__((ifunc("cudaDeviceGraphMemTrim__res")));
static void* cudaDeviceGraphMemTrim__res(void) { return _sym("cudaDeviceGraphMemTrim"); }
void* cudaDeviceRegisterAsyncNotification() __attribute__((ifunc("cudaDeviceRegisterAsyncNotification__res")));
static void* cudaDeviceRegisterAsyncNotification__res(void) { return _sym("cudaDeviceRegisterAsyncNotification"); }
void* cudaDeviceReset() __attribute__((ifunc("cudaDeviceReset__res")));
static void* cudaDeviceReset__res(void) { return _sym("cudaDeviceReset"); }
void* cudaDeviceSetCacheConfig() __attribute__((ifunc("cudaDeviceSetCacheConfig__res")));
static void* cudaDeviceSetCacheConfig__res(void) { return _sym("cudaDeviceSetCacheConfig"); }
void* cudaDeviceSetGraphMemAttribute() __attribute__((ifunc("cudaDeviceSetGraphMemAttribute__res")));
static void* cudaDeviceSetGraphMemAttribute__res(void) { return _sym("cudaDeviceSetGraphMemAttribute"); }
void* cudaDeviceSetLimit() __attribute__((ifunc("cudaDeviceSetLimit__res")));
static void* cudaDeviceSetLimit__res(void) { return _sym("cudaDeviceSetLimit"); }
void* cudaDeviceSetMemPool() __attribute__((ifunc("cudaDeviceSetMemPool__res")));
static void* cudaDeviceSetMemPool__res(void) { return _sym("cudaDeviceSetMemPool"); }
void* cudaDeviceSetSharedMemConfig() __attribute__((ifunc("cudaDeviceSetSharedMemConfig__res")));
static void* cudaDeviceSetSharedMemConfig__res(void) { return _sym("cudaDeviceSetSharedMemConfig"); }
void* cudaDeviceSynchronize() __attribute__((ifunc("cudaDeviceSynchronize__res")));
static void* cudaDeviceSynchronize__res(void) { return _sym("cudaDeviceSynchronize"); }
void* cudaDeviceUnregisterAsyncNotification() __attribute__((ifunc("cudaDeviceUnregisterAsyncNotification__res")));
static void* cudaDeviceUnregisterAsyncNotification__res(void) { return _sym("cudaDeviceUnregisterAsyncNotification"); }
void* cudaDriverGetVersion() __attribute__((ifunc("cudaDriverGetVersion__res")));
static void* cudaDriverGetVersion__res(void) { return _sym("cudaDriverGetVersion"); }
void* cudaEGLStreamConsumerAcquireFrame() __attribute__((ifunc("cudaEGLStreamConsumerAcquireFrame__res")));
static void* cudaEGLStreamConsumerAcquireFrame__res(void) { return _sym("cudaEGLStreamConsumerAcquireFrame"); }
void* cudaEGLStreamConsumerConnect() __attribute__((ifunc("cudaEGLStreamConsumerConnect__res")));
static void* cudaEGLStreamConsumerConnect__res(void) { return _sym("cudaEGLStreamConsumerConnect"); }
void* cudaEGLStreamConsumerConnectWithFlags() __attribute__((ifunc("cudaEGLStreamConsumerConnectWithFlags__res")));
static void* cudaEGLStreamConsumerConnectWithFlags__res(void) { return _sym("cudaEGLStreamConsumerConnectWithFlags"); }
void* cudaEGLStreamConsumerDisconnect() __attribute__((ifunc("cudaEGLStreamConsumerDisconnect__res")));
static void* cudaEGLStreamConsumerDisconnect__res(void) { return _sym("cudaEGLStreamConsumerDisconnect"); }
void* cudaEGLStreamConsumerReleaseFrame() __attribute__((ifunc("cudaEGLStreamConsumerReleaseFrame__res")));
static void* cudaEGLStreamConsumerReleaseFrame__res(void) { return _sym("cudaEGLStreamConsumerReleaseFrame"); }
void* cudaEGLStreamProducerConnect() __attribute__((ifunc("cudaEGLStreamProducerConnect__res")));
static void* cudaEGLStreamProducerConnect__res(void) { return _sym("cudaEGLStreamProducerConnect"); }
void* cudaEGLStreamProducerDisconnect() __attribute__((ifunc("cudaEGLStreamProducerDisconnect__res")));
static void* cudaEGLStreamProducerDisconnect__res(void) { return _sym("cudaEGLStreamProducerDisconnect"); }
void* cudaEGLStreamProducerPresentFrame() __attribute__((ifunc("cudaEGLStreamProducerPresentFrame__res")));
static void* cudaEGLStreamProducerPresentFrame__res(void) { return _sym("cudaEGLStreamProducerPresentFrame"); }
void* cudaEGLStreamProducerReturnFrame() __attribute__((ifunc("cudaEGLStreamProducerReturnFrame__res")));
static void* cudaEGLStreamProducerReturnFrame__res(void) { return _sym("cudaEGLStreamProducerReturnFrame"); }
void* cudaEventCreate() __attribute__((ifunc("cudaEventCreate__res")));
static void* cudaEventCreate__res(void) { return _sym("cudaEventCreate"); }
void* cudaEventCreateFromEGLSync() __attribute__((ifunc("cudaEventCreateFromEGLSync__res")));
static void* cudaEventCreateFromEGLSync__res(void) { return _sym("cudaEventCreateFromEGLSync"); }
void* cudaEventCreateWithFlags() __attribute__((ifunc("cudaEventCreateWithFlags__res")));
static void* cudaEventCreateWithFlags__res(void) { return _sym("cudaEventCreateWithFlags"); }
void* cudaEventDestroy() __attribute__((ifunc("cudaEventDestroy__res")));
static void* cudaEventDestroy__res(void) { return _sym("cudaEventDestroy"); }
void* cudaEventElapsedTime() __attribute__((ifunc("cudaEventElapsedTime__res")));
static void* cudaEventElapsedTime__res(void) { return _sym("cudaEventElapsedTime"); }
void* cudaEventQuery() __attribute__((ifunc("cudaEventQuery__res")));
static void* cudaEventQuery__res(void) { return _sym("cudaEventQuery"); }
void* cudaEventRecord() __attribute__((ifunc("cudaEventRecord__res")));
static void* cudaEventRecord__res(void) { return _sym("cudaEventRecord"); }
void* cudaEventRecord_ptsz() __attribute__((ifunc("cudaEventRecord_ptsz__res")));
static void* cudaEventRecord_ptsz__res(void) { return _sym("cudaEventRecord_ptsz"); }
void* cudaEventRecordWithFlags() __attribute__((ifunc("cudaEventRecordWithFlags__res")));
static void* cudaEventRecordWithFlags__res(void) { return _sym("cudaEventRecordWithFlags"); }
void* cudaEventRecordWithFlags_ptsz() __attribute__((ifunc("cudaEventRecordWithFlags_ptsz__res")));
static void* cudaEventRecordWithFlags_ptsz__res(void) { return _sym("cudaEventRecordWithFlags_ptsz"); }
void* cudaEventSynchronize() __attribute__((ifunc("cudaEventSynchronize__res")));
static void* cudaEventSynchronize__res(void) { return _sym("cudaEventSynchronize"); }
void* cudaExternalMemoryGetMappedBuffer() __attribute__((ifunc("cudaExternalMemoryGetMappedBuffer__res")));
static void* cudaExternalMemoryGetMappedBuffer__res(void) { return _sym("cudaExternalMemoryGetMappedBuffer"); }
void* cudaExternalMemoryGetMappedMipmappedArray() __attribute__((ifunc("cudaExternalMemoryGetMappedMipmappedArray__res")));
static void* cudaExternalMemoryGetMappedMipmappedArray__res(void) { return _sym("cudaExternalMemoryGetMappedMipmappedArray"); }
void* cudaFreeArray() __attribute__((ifunc("cudaFreeArray__res")));
static void* cudaFreeArray__res(void) { return _sym("cudaFreeArray"); }
void* cudaFreeAsync_ptsz() __attribute__((ifunc("cudaFreeAsync_ptsz__res")));
static void* cudaFreeAsync_ptsz__res(void) { return _sym("cudaFreeAsync_ptsz"); }
void* cudaFreeHost() __attribute__((ifunc("cudaFreeHost__res")));
static void* cudaFreeHost__res(void) { return _sym("cudaFreeHost"); }
void* cudaFreeMipmappedArray() __attribute__((ifunc("cudaFreeMipmappedArray__res")));
static void* cudaFreeMipmappedArray__res(void) { return _sym("cudaFreeMipmappedArray"); }
void* cudaFuncGetAttributes() __attribute__((ifunc("cudaFuncGetAttributes__res")));
static void* cudaFuncGetAttributes__res(void) { return _sym("cudaFuncGetAttributes"); }
void* cudaFuncGetName() __attribute__((ifunc("cudaFuncGetName__res")));
static void* cudaFuncGetName__res(void) { return _sym("cudaFuncGetName"); }
void* cudaFuncGetParamInfo() __attribute__((ifunc("cudaFuncGetParamInfo__res")));
static void* cudaFuncGetParamInfo__res(void) { return _sym("cudaFuncGetParamInfo"); }
void* cudaFuncSetAttribute() __attribute__((ifunc("cudaFuncSetAttribute__res")));
static void* cudaFuncSetAttribute__res(void) { return _sym("cudaFuncSetAttribute"); }
void* cudaFuncSetCacheConfig() __attribute__((ifunc("cudaFuncSetCacheConfig__res")));
static void* cudaFuncSetCacheConfig__res(void) { return _sym("cudaFuncSetCacheConfig"); }
void* cudaFuncSetSharedMemConfig() __attribute__((ifunc("cudaFuncSetSharedMemConfig__res")));
static void* cudaFuncSetSharedMemConfig__res(void) { return _sym("cudaFuncSetSharedMemConfig"); }
void* cudaGetChannelDesc() __attribute__((ifunc("cudaGetChannelDesc__res")));
static void* cudaGetChannelDesc__res(void) { return _sym("cudaGetChannelDesc"); }
void* cudaGetDevice() __attribute__((ifunc("cudaGetDevice__res")));
static void* cudaGetDevice__res(void) { return _sym("cudaGetDevice"); }
void* cudaGetDeviceFlags() __attribute__((ifunc("cudaGetDeviceFlags__res")));
static void* cudaGetDeviceFlags__res(void) { return _sym("cudaGetDeviceFlags"); }
void* cudaGetDeviceProperties() __attribute__((ifunc("cudaGetDeviceProperties__res")));
static void* cudaGetDeviceProperties__res(void) { return _sym("cudaGetDeviceProperties"); }
void* cudaGetDeviceProperties_v2() __attribute__((ifunc("cudaGetDeviceProperties_v2__res")));
static void* cudaGetDeviceProperties_v2__res(void) { return _sym("cudaGetDeviceProperties_v2"); }
void* cudaGetDriverEntryPoint() __attribute__((ifunc("cudaGetDriverEntryPoint__res")));
static void* cudaGetDriverEntryPoint__res(void) { return _sym("cudaGetDriverEntryPoint"); }
void* cudaGetDriverEntryPointByVersion() __attribute__((ifunc("cudaGetDriverEntryPointByVersion__res")));
static void* cudaGetDriverEntryPointByVersion__res(void) { return _sym("cudaGetDriverEntryPointByVersion"); }
void* cudaGetDriverEntryPointByVersion_ptsz() __attribute__((ifunc("cudaGetDriverEntryPointByVersion_ptsz__res")));
static void* cudaGetDriverEntryPointByVersion_ptsz__res(void) { return _sym("cudaGetDriverEntryPointByVersion_ptsz"); }
void* cudaGetDriverEntryPoint_ptsz() __attribute__((ifunc("cudaGetDriverEntryPoint_ptsz__res")));
static void* cudaGetDriverEntryPoint_ptsz__res(void) { return _sym("cudaGetDriverEntryPoint_ptsz"); }
void* cudaGetErrorName() __attribute__((ifunc("cudaGetErrorName__res")));
static void* cudaGetErrorName__res(void) { return _sym("cudaGetErrorName"); }
void* cudaGetErrorString() __attribute__((ifunc("cudaGetErrorString__res")));
static void* cudaGetErrorString__res(void) { return _sym("cudaGetErrorString"); }
void* cudaGetExportTable() __attribute__((ifunc("cudaGetExportTable__res")));
static void* cudaGetExportTable__res(void) { return _sym("cudaGetExportTable"); }
void* cudaGetFuncBySymbol() __attribute__((ifunc("cudaGetFuncBySymbol__res")));
static void* cudaGetFuncBySymbol__res(void) { return _sym("cudaGetFuncBySymbol"); }
void* __cudaGetKernel() __attribute__((ifunc("__cudaGetKernel__res")));
static void* __cudaGetKernel__res(void) { return _sym("__cudaGetKernel"); }
void* cudaGetKernel() __attribute__((ifunc("cudaGetKernel__res")));
static void* cudaGetKernel__res(void) { return _sym("cudaGetKernel"); }
void* cudaGetLastError() __attribute__((ifunc("cudaGetLastError__res")));
static void* cudaGetLastError__res(void) { return _sym("cudaGetLastError"); }
void* cudaGetMipmappedArrayLevel() __attribute__((ifunc("cudaGetMipmappedArrayLevel__res")));
static void* cudaGetMipmappedArrayLevel__res(void) { return _sym("cudaGetMipmappedArrayLevel"); }
void* cudaGetSurfaceObjectResourceDesc() __attribute__((ifunc("cudaGetSurfaceObjectResourceDesc__res")));
static void* cudaGetSurfaceObjectResourceDesc__res(void) { return _sym("cudaGetSurfaceObjectResourceDesc"); }
void* cudaGetSymbolAddress() __attribute__((ifunc("cudaGetSymbolAddress__res")));
static void* cudaGetSymbolAddress__res(void) { return _sym("cudaGetSymbolAddress"); }
void* cudaGetSymbolSize() __attribute__((ifunc("cudaGetSymbolSize__res")));
static void* cudaGetSymbolSize__res(void) { return _sym("cudaGetSymbolSize"); }
void* cudaGetTextureObjectResourceDesc() __attribute__((ifunc("cudaGetTextureObjectResourceDesc__res")));
static void* cudaGetTextureObjectResourceDesc__res(void) { return _sym("cudaGetTextureObjectResourceDesc"); }
void* cudaGetTextureObjectResourceViewDesc() __attribute__((ifunc("cudaGetTextureObjectResourceViewDesc__res")));
static void* cudaGetTextureObjectResourceViewDesc__res(void) { return _sym("cudaGetTextureObjectResourceViewDesc"); }
void* cudaGetTextureObjectTextureDesc() __attribute__((ifunc("cudaGetTextureObjectTextureDesc__res")));
static void* cudaGetTextureObjectTextureDesc__res(void) { return _sym("cudaGetTextureObjectTextureDesc"); }
void* cudaGLGetDevices() __attribute__((ifunc("cudaGLGetDevices__res")));
static void* cudaGLGetDevices__res(void) { return _sym("cudaGLGetDevices"); }
void* cudaGLMapBufferObject() __attribute__((ifunc("cudaGLMapBufferObject__res")));
static void* cudaGLMapBufferObject__res(void) { return _sym("cudaGLMapBufferObject"); }
void* cudaGLMapBufferObjectAsync() __attribute__((ifunc("cudaGLMapBufferObjectAsync__res")));
static void* cudaGLMapBufferObjectAsync__res(void) { return _sym("cudaGLMapBufferObjectAsync"); }
void* cudaGLRegisterBufferObject() __attribute__((ifunc("cudaGLRegisterBufferObject__res")));
static void* cudaGLRegisterBufferObject__res(void) { return _sym("cudaGLRegisterBufferObject"); }
void* cudaGLSetBufferObjectMapFlags() __attribute__((ifunc("cudaGLSetBufferObjectMapFlags__res")));
static void* cudaGLSetBufferObjectMapFlags__res(void) { return _sym("cudaGLSetBufferObjectMapFlags"); }
void* cudaGLSetGLDevice() __attribute__((ifunc("cudaGLSetGLDevice__res")));
static void* cudaGLSetGLDevice__res(void) { return _sym("cudaGLSetGLDevice"); }
void* cudaGLUnmapBufferObject() __attribute__((ifunc("cudaGLUnmapBufferObject__res")));
static void* cudaGLUnmapBufferObject__res(void) { return _sym("cudaGLUnmapBufferObject"); }
void* cudaGLUnmapBufferObjectAsync() __attribute__((ifunc("cudaGLUnmapBufferObjectAsync__res")));
static void* cudaGLUnmapBufferObjectAsync__res(void) { return _sym("cudaGLUnmapBufferObjectAsync"); }
void* cudaGLUnregisterBufferObject() __attribute__((ifunc("cudaGLUnregisterBufferObject__res")));
static void* cudaGLUnregisterBufferObject__res(void) { return _sym("cudaGLUnregisterBufferObject"); }
void* cudaGraphAddChildGraphNode() __attribute__((ifunc("cudaGraphAddChildGraphNode__res")));
static void* cudaGraphAddChildGraphNode__res(void) { return _sym("cudaGraphAddChildGraphNode"); }
void* cudaGraphAddDependencies() __attribute__((ifunc("cudaGraphAddDependencies__res")));
static void* cudaGraphAddDependencies__res(void) { return _sym("cudaGraphAddDependencies"); }
void* cudaGraphAddDependencies_v2() __attribute__((ifunc("cudaGraphAddDependencies_v2__res")));
static void* cudaGraphAddDependencies_v2__res(void) { return _sym("cudaGraphAddDependencies_v2"); }
void* cudaGraphAddEmptyNode() __attribute__((ifunc("cudaGraphAddEmptyNode__res")));
static void* cudaGraphAddEmptyNode__res(void) { return _sym("cudaGraphAddEmptyNode"); }
void* cudaGraphAddEventRecordNode() __attribute__((ifunc("cudaGraphAddEventRecordNode__res")));
static void* cudaGraphAddEventRecordNode__res(void) { return _sym("cudaGraphAddEventRecordNode"); }
void* cudaGraphAddEventWaitNode() __attribute__((ifunc("cudaGraphAddEventWaitNode__res")));
static void* cudaGraphAddEventWaitNode__res(void) { return _sym("cudaGraphAddEventWaitNode"); }
void* cudaGraphAddExternalSemaphoresSignalNode() __attribute__((ifunc("cudaGraphAddExternalSemaphoresSignalNode__res")));
static void* cudaGraphAddExternalSemaphoresSignalNode__res(void) { return _sym("cudaGraphAddExternalSemaphoresSignalNode"); }
void* cudaGraphAddExternalSemaphoresWaitNode() __attribute__((ifunc("cudaGraphAddExternalSemaphoresWaitNode__res")));
static void* cudaGraphAddExternalSemaphoresWaitNode__res(void) { return _sym("cudaGraphAddExternalSemaphoresWaitNode"); }
void* cudaGraphAddHostNode() __attribute__((ifunc("cudaGraphAddHostNode__res")));
static void* cudaGraphAddHostNode__res(void) { return _sym("cudaGraphAddHostNode"); }
void* cudaGraphAddKernelNode() __attribute__((ifunc("cudaGraphAddKernelNode__res")));
static void* cudaGraphAddKernelNode__res(void) { return _sym("cudaGraphAddKernelNode"); }
void* cudaGraphAddMemAllocNode() __attribute__((ifunc("cudaGraphAddMemAllocNode__res")));
static void* cudaGraphAddMemAllocNode__res(void) { return _sym("cudaGraphAddMemAllocNode"); }
void* cudaGraphAddMemcpyNode() __attribute__((ifunc("cudaGraphAddMemcpyNode__res")));
static void* cudaGraphAddMemcpyNode__res(void) { return _sym("cudaGraphAddMemcpyNode"); }
void* cudaGraphAddMemcpyNode1D() __attribute__((ifunc("cudaGraphAddMemcpyNode1D__res")));
static void* cudaGraphAddMemcpyNode1D__res(void) { return _sym("cudaGraphAddMemcpyNode1D"); }
void* cudaGraphAddMemcpyNodeFromSymbol() __attribute__((ifunc("cudaGraphAddMemcpyNodeFromSymbol__res")));
static void* cudaGraphAddMemcpyNodeFromSymbol__res(void) { return _sym("cudaGraphAddMemcpyNodeFromSymbol"); }
void* cudaGraphAddMemcpyNodeToSymbol() __attribute__((ifunc("cudaGraphAddMemcpyNodeToSymbol__res")));
static void* cudaGraphAddMemcpyNodeToSymbol__res(void) { return _sym("cudaGraphAddMemcpyNodeToSymbol"); }
void* cudaGraphAddMemFreeNode() __attribute__((ifunc("cudaGraphAddMemFreeNode__res")));
static void* cudaGraphAddMemFreeNode__res(void) { return _sym("cudaGraphAddMemFreeNode"); }
void* cudaGraphAddMemsetNode() __attribute__((ifunc("cudaGraphAddMemsetNode__res")));
static void* cudaGraphAddMemsetNode__res(void) { return _sym("cudaGraphAddMemsetNode"); }
void* cudaGraphAddNode() __attribute__((ifunc("cudaGraphAddNode__res")));
static void* cudaGraphAddNode__res(void) { return _sym("cudaGraphAddNode"); }
void* cudaGraphAddNode_v2() __attribute__((ifunc("cudaGraphAddNode_v2__res")));
static void* cudaGraphAddNode_v2__res(void) { return _sym("cudaGraphAddNode_v2"); }
void* cudaGraphChildGraphNodeGetGraph() __attribute__((ifunc("cudaGraphChildGraphNodeGetGraph__res")));
static void* cudaGraphChildGraphNodeGetGraph__res(void) { return _sym("cudaGraphChildGraphNodeGetGraph"); }
void* cudaGraphClone() __attribute__((ifunc("cudaGraphClone__res")));
static void* cudaGraphClone__res(void) { return _sym("cudaGraphClone"); }
void* cudaGraphConditionalHandleCreate() __attribute__((ifunc("cudaGraphConditionalHandleCreate__res")));
static void* cudaGraphConditionalHandleCreate__res(void) { return _sym("cudaGraphConditionalHandleCreate"); }
void* cudaGraphCreate() __attribute__((ifunc("cudaGraphCreate__res")));
static void* cudaGraphCreate__res(void) { return _sym("cudaGraphCreate"); }
void* cudaGraphDebugDotPrint() __attribute__((ifunc("cudaGraphDebugDotPrint__res")));
static void* cudaGraphDebugDotPrint__res(void) { return _sym("cudaGraphDebugDotPrint"); }
void* cudaGraphDestroy() __attribute__((ifunc("cudaGraphDestroy__res")));
static void* cudaGraphDestroy__res(void) { return _sym("cudaGraphDestroy"); }
void* cudaGraphDestroyNode() __attribute__((ifunc("cudaGraphDestroyNode__res")));
static void* cudaGraphDestroyNode__res(void) { return _sym("cudaGraphDestroyNode"); }
void* cudaGraphEventRecordNodeGetEvent() __attribute__((ifunc("cudaGraphEventRecordNodeGetEvent__res")));
static void* cudaGraphEventRecordNodeGetEvent__res(void) { return _sym("cudaGraphEventRecordNodeGetEvent"); }
void* cudaGraphEventRecordNodeSetEvent() __attribute__((ifunc("cudaGraphEventRecordNodeSetEvent__res")));
static void* cudaGraphEventRecordNodeSetEvent__res(void) { return _sym("cudaGraphEventRecordNodeSetEvent"); }
void* cudaGraphEventWaitNodeGetEvent() __attribute__((ifunc("cudaGraphEventWaitNodeGetEvent__res")));
static void* cudaGraphEventWaitNodeGetEvent__res(void) { return _sym("cudaGraphEventWaitNodeGetEvent"); }
void* cudaGraphEventWaitNodeSetEvent() __attribute__((ifunc("cudaGraphEventWaitNodeSetEvent__res")));
static void* cudaGraphEventWaitNodeSetEvent__res(void) { return _sym("cudaGraphEventWaitNodeSetEvent"); }
void* cudaGraphExecChildGraphNodeSetParams() __attribute__((ifunc("cudaGraphExecChildGraphNodeSetParams__res")));
static void* cudaGraphExecChildGraphNodeSetParams__res(void) { return _sym("cudaGraphExecChildGraphNodeSetParams"); }
void* cudaGraphExecDestroy() __attribute__((ifunc("cudaGraphExecDestroy__res")));
static void* cudaGraphExecDestroy__res(void) { return _sym("cudaGraphExecDestroy"); }
void* cudaGraphExecEventRecordNodeSetEvent() __attribute__((ifunc("cudaGraphExecEventRecordNodeSetEvent__res")));
static void* cudaGraphExecEventRecordNodeSetEvent__res(void) { return _sym("cudaGraphExecEventRecordNodeSetEvent"); }
void* cudaGraphExecEventWaitNodeSetEvent() __attribute__((ifunc("cudaGraphExecEventWaitNodeSetEvent__res")));
static void* cudaGraphExecEventWaitNodeSetEvent__res(void) { return _sym("cudaGraphExecEventWaitNodeSetEvent"); }
void* cudaGraphExecExternalSemaphoresSignalNodeSetParams() __attribute__((ifunc("cudaGraphExecExternalSemaphoresSignalNodeSetParams__res")));
static void* cudaGraphExecExternalSemaphoresSignalNodeSetParams__res(void) { return _sym("cudaGraphExecExternalSemaphoresSignalNodeSetParams"); }
void* cudaGraphExecExternalSemaphoresWaitNodeSetParams() __attribute__((ifunc("cudaGraphExecExternalSemaphoresWaitNodeSetParams__res")));
static void* cudaGraphExecExternalSemaphoresWaitNodeSetParams__res(void) { return _sym("cudaGraphExecExternalSemaphoresWaitNodeSetParams"); }
void* cudaGraphExecGetFlags() __attribute__((ifunc("cudaGraphExecGetFlags__res")));
static void* cudaGraphExecGetFlags__res(void) { return _sym("cudaGraphExecGetFlags"); }
void* cudaGraphExecHostNodeSetParams() __attribute__((ifunc("cudaGraphExecHostNodeSetParams__res")));
static void* cudaGraphExecHostNodeSetParams__res(void) { return _sym("cudaGraphExecHostNodeSetParams"); }
void* cudaGraphExecKernelNodeSetParams() __attribute__((ifunc("cudaGraphExecKernelNodeSetParams__res")));
static void* cudaGraphExecKernelNodeSetParams__res(void) { return _sym("cudaGraphExecKernelNodeSetParams"); }
void* cudaGraphExecMemcpyNodeSetParams() __attribute__((ifunc("cudaGraphExecMemcpyNodeSetParams__res")));
static void* cudaGraphExecMemcpyNodeSetParams__res(void) { return _sym("cudaGraphExecMemcpyNodeSetParams"); }
void* cudaGraphExecMemcpyNodeSetParams1D() __attribute__((ifunc("cudaGraphExecMemcpyNodeSetParams1D__res")));
static void* cudaGraphExecMemcpyNodeSetParams1D__res(void) { return _sym("cudaGraphExecMemcpyNodeSetParams1D"); }
void* cudaGraphExecMemcpyNodeSetParamsFromSymbol() __attribute__((ifunc("cudaGraphExecMemcpyNodeSetParamsFromSymbol__res")));
static void* cudaGraphExecMemcpyNodeSetParamsFromSymbol__res(void) { return _sym("cudaGraphExecMemcpyNodeSetParamsFromSymbol"); }
void* cudaGraphExecMemcpyNodeSetParamsToSymbol() __attribute__((ifunc("cudaGraphExecMemcpyNodeSetParamsToSymbol__res")));
static void* cudaGraphExecMemcpyNodeSetParamsToSymbol__res(void) { return _sym("cudaGraphExecMemcpyNodeSetParamsToSymbol"); }
void* cudaGraphExecMemsetNodeSetParams() __attribute__((ifunc("cudaGraphExecMemsetNodeSetParams__res")));
static void* cudaGraphExecMemsetNodeSetParams__res(void) { return _sym("cudaGraphExecMemsetNodeSetParams"); }
void* cudaGraphExecNodeSetParams() __attribute__((ifunc("cudaGraphExecNodeSetParams__res")));
static void* cudaGraphExecNodeSetParams__res(void) { return _sym("cudaGraphExecNodeSetParams"); }
void* cudaGraphExecUpdate() __attribute__((ifunc("cudaGraphExecUpdate__res")));
static void* cudaGraphExecUpdate__res(void) { return _sym("cudaGraphExecUpdate"); }
void* cudaGraphExternalSemaphoresSignalNodeGetParams() __attribute__((ifunc("cudaGraphExternalSemaphoresSignalNodeGetParams__res")));
static void* cudaGraphExternalSemaphoresSignalNodeGetParams__res(void) { return _sym("cudaGraphExternalSemaphoresSignalNodeGetParams"); }
void* cudaGraphExternalSemaphoresSignalNodeSetParams() __attribute__((ifunc("cudaGraphExternalSemaphoresSignalNodeSetParams__res")));
static void* cudaGraphExternalSemaphoresSignalNodeSetParams__res(void) { return _sym("cudaGraphExternalSemaphoresSignalNodeSetParams"); }
void* cudaGraphExternalSemaphoresWaitNodeGetParams() __attribute__((ifunc("cudaGraphExternalSemaphoresWaitNodeGetParams__res")));
static void* cudaGraphExternalSemaphoresWaitNodeGetParams__res(void) { return _sym("cudaGraphExternalSemaphoresWaitNodeGetParams"); }
void* cudaGraphExternalSemaphoresWaitNodeSetParams() __attribute__((ifunc("cudaGraphExternalSemaphoresWaitNodeSetParams__res")));
static void* cudaGraphExternalSemaphoresWaitNodeSetParams__res(void) { return _sym("cudaGraphExternalSemaphoresWaitNodeSetParams"); }
void* cudaGraphGetEdges() __attribute__((ifunc("cudaGraphGetEdges__res")));
static void* cudaGraphGetEdges__res(void) { return _sym("cudaGraphGetEdges"); }
void* cudaGraphGetEdges_v2() __attribute__((ifunc("cudaGraphGetEdges_v2__res")));
static void* cudaGraphGetEdges_v2__res(void) { return _sym("cudaGraphGetEdges_v2"); }
void* cudaGraphGetNodes() __attribute__((ifunc("cudaGraphGetNodes__res")));
static void* cudaGraphGetNodes__res(void) { return _sym("cudaGraphGetNodes"); }
void* cudaGraphGetRootNodes() __attribute__((ifunc("cudaGraphGetRootNodes__res")));
static void* cudaGraphGetRootNodes__res(void) { return _sym("cudaGraphGetRootNodes"); }
void* cudaGraphHostNodeGetParams() __attribute__((ifunc("cudaGraphHostNodeGetParams__res")));
static void* cudaGraphHostNodeGetParams__res(void) { return _sym("cudaGraphHostNodeGetParams"); }
void* cudaGraphHostNodeSetParams() __attribute__((ifunc("cudaGraphHostNodeSetParams__res")));
static void* cudaGraphHostNodeSetParams__res(void) { return _sym("cudaGraphHostNodeSetParams"); }
void* cudaGraphicsEGLRegisterImage() __attribute__((ifunc("cudaGraphicsEGLRegisterImage__res")));
static void* cudaGraphicsEGLRegisterImage__res(void) { return _sym("cudaGraphicsEGLRegisterImage"); }
void* cudaGraphicsGLRegisterBuffer() __attribute__((ifunc("cudaGraphicsGLRegisterBuffer__res")));
static void* cudaGraphicsGLRegisterBuffer__res(void) { return _sym("cudaGraphicsGLRegisterBuffer"); }
void* cudaGraphicsGLRegisterImage() __attribute__((ifunc("cudaGraphicsGLRegisterImage__res")));
static void* cudaGraphicsGLRegisterImage__res(void) { return _sym("cudaGraphicsGLRegisterImage"); }
void* cudaGraphicsMapResources() __attribute__((ifunc("cudaGraphicsMapResources__res")));
static void* cudaGraphicsMapResources__res(void) { return _sym("cudaGraphicsMapResources"); }
void* cudaGraphicsResourceGetMappedEglFrame() __attribute__((ifunc("cudaGraphicsResourceGetMappedEglFrame__res")));
static void* cudaGraphicsResourceGetMappedEglFrame__res(void) { return _sym("cudaGraphicsResourceGetMappedEglFrame"); }
void* cudaGraphicsResourceGetMappedMipmappedArray() __attribute__((ifunc("cudaGraphicsResourceGetMappedMipmappedArray__res")));
static void* cudaGraphicsResourceGetMappedMipmappedArray__res(void) { return _sym("cudaGraphicsResourceGetMappedMipmappedArray"); }
void* cudaGraphicsResourceGetMappedPointer() __attribute__((ifunc("cudaGraphicsResourceGetMappedPointer__res")));
static void* cudaGraphicsResourceGetMappedPointer__res(void) { return _sym("cudaGraphicsResourceGetMappedPointer"); }
void* cudaGraphicsResourceSetMapFlags() __attribute__((ifunc("cudaGraphicsResourceSetMapFlags__res")));
static void* cudaGraphicsResourceSetMapFlags__res(void) { return _sym("cudaGraphicsResourceSetMapFlags"); }
void* cudaGraphicsSubResourceGetMappedArray() __attribute__((ifunc("cudaGraphicsSubResourceGetMappedArray__res")));
static void* cudaGraphicsSubResourceGetMappedArray__res(void) { return _sym("cudaGraphicsSubResourceGetMappedArray"); }
void* cudaGraphicsUnmapResources() __attribute__((ifunc("cudaGraphicsUnmapResources__res")));
static void* cudaGraphicsUnmapResources__res(void) { return _sym("cudaGraphicsUnmapResources"); }
void* cudaGraphicsUnregisterResource() __attribute__((ifunc("cudaGraphicsUnregisterResource__res")));
static void* cudaGraphicsUnregisterResource__res(void) { return _sym("cudaGraphicsUnregisterResource"); }
void* cudaGraphicsVDPAURegisterOutputSurface() __attribute__((ifunc("cudaGraphicsVDPAURegisterOutputSurface__res")));
static void* cudaGraphicsVDPAURegisterOutputSurface__res(void) { return _sym("cudaGraphicsVDPAURegisterOutputSurface"); }
void* cudaGraphicsVDPAURegisterVideoSurface() __attribute__((ifunc("cudaGraphicsVDPAURegisterVideoSurface__res")));
static void* cudaGraphicsVDPAURegisterVideoSurface__res(void) { return _sym("cudaGraphicsVDPAURegisterVideoSurface"); }
void* cudaGraphInstantiate() __attribute__((ifunc("cudaGraphInstantiate__res")));
static void* cudaGraphInstantiate__res(void) { return _sym("cudaGraphInstantiate"); }
void* cudaGraphInstantiateWithFlags() __attribute__((ifunc("cudaGraphInstantiateWithFlags__res")));
static void* cudaGraphInstantiateWithFlags__res(void) { return _sym("cudaGraphInstantiateWithFlags"); }
void* cudaGraphInstantiateWithParams() __attribute__((ifunc("cudaGraphInstantiateWithParams__res")));
static void* cudaGraphInstantiateWithParams__res(void) { return _sym("cudaGraphInstantiateWithParams"); }
void* cudaGraphInstantiateWithParams_ptsz() __attribute__((ifunc("cudaGraphInstantiateWithParams_ptsz__res")));
static void* cudaGraphInstantiateWithParams_ptsz__res(void) { return _sym("cudaGraphInstantiateWithParams_ptsz"); }
void* cudaGraphKernelNodeCopyAttributes() __attribute__((ifunc("cudaGraphKernelNodeCopyAttributes__res")));
static void* cudaGraphKernelNodeCopyAttributes__res(void) { return _sym("cudaGraphKernelNodeCopyAttributes"); }
void* cudaGraphKernelNodeGetAttribute() __attribute__((ifunc("cudaGraphKernelNodeGetAttribute__res")));
static void* cudaGraphKernelNodeGetAttribute__res(void) { return _sym("cudaGraphKernelNodeGetAttribute"); }
void* cudaGraphKernelNodeGetParams() __attribute__((ifunc("cudaGraphKernelNodeGetParams__res")));
static void* cudaGraphKernelNodeGetParams__res(void) { return _sym("cudaGraphKernelNodeGetParams"); }
void* cudaGraphKernelNodeSetAttribute() __attribute__((ifunc("cudaGraphKernelNodeSetAttribute__res")));
static void* cudaGraphKernelNodeSetAttribute__res(void) { return _sym("cudaGraphKernelNodeSetAttribute"); }
void* cudaGraphKernelNodeSetParams() __attribute__((ifunc("cudaGraphKernelNodeSetParams__res")));
static void* cudaGraphKernelNodeSetParams__res(void) { return _sym("cudaGraphKernelNodeSetParams"); }
void* cudaGraphLaunch() __attribute__((ifunc("cudaGraphLaunch__res")));
static void* cudaGraphLaunch__res(void) { return _sym("cudaGraphLaunch"); }
void* cudaGraphLaunch_ptsz() __attribute__((ifunc("cudaGraphLaunch_ptsz__res")));
static void* cudaGraphLaunch_ptsz__res(void) { return _sym("cudaGraphLaunch_ptsz"); }
void* cudaGraphMemAllocNodeGetParams() __attribute__((ifunc("cudaGraphMemAllocNodeGetParams__res")));
static void* cudaGraphMemAllocNodeGetParams__res(void) { return _sym("cudaGraphMemAllocNodeGetParams"); }
void* cudaGraphMemcpyNodeGetParams() __attribute__((ifunc("cudaGraphMemcpyNodeGetParams__res")));
static void* cudaGraphMemcpyNodeGetParams__res(void) { return _sym("cudaGraphMemcpyNodeGetParams"); }
void* cudaGraphMemcpyNodeSetParams() __attribute__((ifunc("cudaGraphMemcpyNodeSetParams__res")));
static void* cudaGraphMemcpyNodeSetParams__res(void) { return _sym("cudaGraphMemcpyNodeSetParams"); }
void* cudaGraphMemcpyNodeSetParams1D() __attribute__((ifunc("cudaGraphMemcpyNodeSetParams1D__res")));
static void* cudaGraphMemcpyNodeSetParams1D__res(void) { return _sym("cudaGraphMemcpyNodeSetParams1D"); }
void* cudaGraphMemcpyNodeSetParamsFromSymbol() __attribute__((ifunc("cudaGraphMemcpyNodeSetParamsFromSymbol__res")));
static void* cudaGraphMemcpyNodeSetParamsFromSymbol__res(void) { return _sym("cudaGraphMemcpyNodeSetParamsFromSymbol"); }
void* cudaGraphMemcpyNodeSetParamsToSymbol() __attribute__((ifunc("cudaGraphMemcpyNodeSetParamsToSymbol__res")));
static void* cudaGraphMemcpyNodeSetParamsToSymbol__res(void) { return _sym("cudaGraphMemcpyNodeSetParamsToSymbol"); }
void* cudaGraphMemFreeNodeGetParams() __attribute__((ifunc("cudaGraphMemFreeNodeGetParams__res")));
static void* cudaGraphMemFreeNodeGetParams__res(void) { return _sym("cudaGraphMemFreeNodeGetParams"); }
void* cudaGraphMemsetNodeGetParams() __attribute__((ifunc("cudaGraphMemsetNodeGetParams__res")));
static void* cudaGraphMemsetNodeGetParams__res(void) { return _sym("cudaGraphMemsetNodeGetParams"); }
void* cudaGraphMemsetNodeSetParams() __attribute__((ifunc("cudaGraphMemsetNodeSetParams__res")));
static void* cudaGraphMemsetNodeSetParams__res(void) { return _sym("cudaGraphMemsetNodeSetParams"); }
void* cudaGraphNodeFindInClone() __attribute__((ifunc("cudaGraphNodeFindInClone__res")));
static void* cudaGraphNodeFindInClone__res(void) { return _sym("cudaGraphNodeFindInClone"); }
void* cudaGraphNodeGetDependencies() __attribute__((ifunc("cudaGraphNodeGetDependencies__res")));
static void* cudaGraphNodeGetDependencies__res(void) { return _sym("cudaGraphNodeGetDependencies"); }
void* cudaGraphNodeGetDependencies_v2() __attribute__((ifunc("cudaGraphNodeGetDependencies_v2__res")));
static void* cudaGraphNodeGetDependencies_v2__res(void) { return _sym("cudaGraphNodeGetDependencies_v2"); }
void* cudaGraphNodeGetDependentNodes() __attribute__((ifunc("cudaGraphNodeGetDependentNodes__res")));
static void* cudaGraphNodeGetDependentNodes__res(void) { return _sym("cudaGraphNodeGetDependentNodes"); }
void* cudaGraphNodeGetDependentNodes_v2() __attribute__((ifunc("cudaGraphNodeGetDependentNodes_v2__res")));
static void* cudaGraphNodeGetDependentNodes_v2__res(void) { return _sym("cudaGraphNodeGetDependentNodes_v2"); }
void* cudaGraphNodeGetEnabled() __attribute__((ifunc("cudaGraphNodeGetEnabled__res")));
static void* cudaGraphNodeGetEnabled__res(void) { return _sym("cudaGraphNodeGetEnabled"); }
void* cudaGraphNodeGetType() __attribute__((ifunc("cudaGraphNodeGetType__res")));
static void* cudaGraphNodeGetType__res(void) { return _sym("cudaGraphNodeGetType"); }
void* cudaGraphNodeSetEnabled() __attribute__((ifunc("cudaGraphNodeSetEnabled__res")));
static void* cudaGraphNodeSetEnabled__res(void) { return _sym("cudaGraphNodeSetEnabled"); }
void* cudaGraphNodeSetParams() __attribute__((ifunc("cudaGraphNodeSetParams__res")));
static void* cudaGraphNodeSetParams__res(void) { return _sym("cudaGraphNodeSetParams"); }
void* cudaGraphReleaseUserObject() __attribute__((ifunc("cudaGraphReleaseUserObject__res")));
static void* cudaGraphReleaseUserObject__res(void) { return _sym("cudaGraphReleaseUserObject"); }
void* cudaGraphRemoveDependencies() __attribute__((ifunc("cudaGraphRemoveDependencies__res")));
static void* cudaGraphRemoveDependencies__res(void) { return _sym("cudaGraphRemoveDependencies"); }
void* cudaGraphRemoveDependencies_v2() __attribute__((ifunc("cudaGraphRemoveDependencies_v2__res")));
static void* cudaGraphRemoveDependencies_v2__res(void) { return _sym("cudaGraphRemoveDependencies_v2"); }
void* cudaGraphRetainUserObject() __attribute__((ifunc("cudaGraphRetainUserObject__res")));
static void* cudaGraphRetainUserObject__res(void) { return _sym("cudaGraphRetainUserObject"); }
void* cudaGraphUpload() __attribute__((ifunc("cudaGraphUpload__res")));
static void* cudaGraphUpload__res(void) { return _sym("cudaGraphUpload"); }
void* cudaGraphUpload_ptsz() __attribute__((ifunc("cudaGraphUpload_ptsz__res")));
static void* cudaGraphUpload_ptsz__res(void) { return _sym("cudaGraphUpload_ptsz"); }
void* cudaHostAlloc() __attribute__((ifunc("cudaHostAlloc__res")));
static void* cudaHostAlloc__res(void) { return _sym("cudaHostAlloc"); }
void* cudaHostGetDevicePointer() __attribute__((ifunc("cudaHostGetDevicePointer__res")));
static void* cudaHostGetDevicePointer__res(void) { return _sym("cudaHostGetDevicePointer"); }
void* cudaHostGetFlags() __attribute__((ifunc("cudaHostGetFlags__res")));
static void* cudaHostGetFlags__res(void) { return _sym("cudaHostGetFlags"); }
void* cudaHostRegister() __attribute__((ifunc("cudaHostRegister__res")));
static void* cudaHostRegister__res(void) { return _sym("cudaHostRegister"); }
void* cudaHostUnregister() __attribute__((ifunc("cudaHostUnregister__res")));
static void* cudaHostUnregister__res(void) { return _sym("cudaHostUnregister"); }
void* cudaImportExternalMemory() __attribute__((ifunc("cudaImportExternalMemory__res")));
static void* cudaImportExternalMemory__res(void) { return _sym("cudaImportExternalMemory"); }
void* cudaImportExternalSemaphore() __attribute__((ifunc("cudaImportExternalSemaphore__res")));
static void* cudaImportExternalSemaphore__res(void) { return _sym("cudaImportExternalSemaphore"); }
void* cudaInitDevice() __attribute__((ifunc("cudaInitDevice__res")));
static void* cudaInitDevice__res(void) { return _sym("cudaInitDevice"); }
void* __cudaInitModule() __attribute__((ifunc("__cudaInitModule__res")));
static void* __cudaInitModule__res(void) { return _sym("__cudaInitModule"); }
void* cudaIpcCloseMemHandle() __attribute__((ifunc("cudaIpcCloseMemHandle__res")));
static void* cudaIpcCloseMemHandle__res(void) { return _sym("cudaIpcCloseMemHandle"); }
void* cudaIpcGetEventHandle() __attribute__((ifunc("cudaIpcGetEventHandle__res")));
static void* cudaIpcGetEventHandle__res(void) { return _sym("cudaIpcGetEventHandle"); }
void* cudaIpcGetMemHandle() __attribute__((ifunc("cudaIpcGetMemHandle__res")));
static void* cudaIpcGetMemHandle__res(void) { return _sym("cudaIpcGetMemHandle"); }
void* cudaIpcOpenEventHandle() __attribute__((ifunc("cudaIpcOpenEventHandle__res")));
static void* cudaIpcOpenEventHandle__res(void) { return _sym("cudaIpcOpenEventHandle"); }
void* cudaIpcOpenMemHandle() __attribute__((ifunc("cudaIpcOpenMemHandle__res")));
static void* cudaIpcOpenMemHandle__res(void) { return _sym("cudaIpcOpenMemHandle"); }
void* cudaLaunchCooperativeKernel() __attribute__((ifunc("cudaLaunchCooperativeKernel__res")));
static void* cudaLaunchCooperativeKernel__res(void) { return _sym("cudaLaunchCooperativeKernel"); }
void* cudaLaunchCooperativeKernelMultiDevice() __attribute__((ifunc("cudaLaunchCooperativeKernelMultiDevice__res")));
static void* cudaLaunchCooperativeKernelMultiDevice__res(void) { return _sym("cudaLaunchCooperativeKernelMultiDevice"); }
void* cudaLaunchCooperativeKernel_ptsz() __attribute__((ifunc("cudaLaunchCooperativeKernel_ptsz__res")));
static void* cudaLaunchCooperativeKernel_ptsz__res(void) { return _sym("cudaLaunchCooperativeKernel_ptsz"); }
void* cudaLaunchHostFunc() __attribute__((ifunc("cudaLaunchHostFunc__res")));
static void* cudaLaunchHostFunc__res(void) { return _sym("cudaLaunchHostFunc"); }
void* cudaLaunchHostFunc_ptsz() __attribute__((ifunc("cudaLaunchHostFunc_ptsz__res")));
static void* cudaLaunchHostFunc_ptsz__res(void) { return _sym("cudaLaunchHostFunc_ptsz"); }
void* __cudaLaunchKernel() __attribute__((ifunc("__cudaLaunchKernel__res")));
static void* __cudaLaunchKernel__res(void) { return _sym("__cudaLaunchKernel"); }
void* cudaLaunchKernel() __attribute__((ifunc("cudaLaunchKernel__res")));
static void* cudaLaunchKernel__res(void) { return _sym("cudaLaunchKernel"); }
void* cudaLaunchKernelExC() __attribute__((ifunc("cudaLaunchKernelExC__res")));
static void* cudaLaunchKernelExC__res(void) { return _sym("cudaLaunchKernelExC"); }
void* cudaLaunchKernelExC_ptsz() __attribute__((ifunc("cudaLaunchKernelExC_ptsz__res")));
static void* cudaLaunchKernelExC_ptsz__res(void) { return _sym("cudaLaunchKernelExC_ptsz"); }
void* __cudaLaunchKernel_ptsz() __attribute__((ifunc("__cudaLaunchKernel_ptsz__res")));
static void* __cudaLaunchKernel_ptsz__res(void) { return _sym("__cudaLaunchKernel_ptsz"); }
void* cudaLaunchKernel_ptsz() __attribute__((ifunc("cudaLaunchKernel_ptsz__res")));
static void* cudaLaunchKernel_ptsz__res(void) { return _sym("cudaLaunchKernel_ptsz"); }
void* cudaMalloc3D() __attribute__((ifunc("cudaMalloc3D__res")));
static void* cudaMalloc3D__res(void) { return _sym("cudaMalloc3D"); }
void* cudaMalloc3DArray() __attribute__((ifunc("cudaMalloc3DArray__res")));
static void* cudaMalloc3DArray__res(void) { return _sym("cudaMalloc3DArray"); }
void* cudaMallocArray() __attribute__((ifunc("cudaMallocArray__res")));
static void* cudaMallocArray__res(void) { return _sym("cudaMallocArray"); }
void* cudaMallocAsync_ptsz() __attribute__((ifunc("cudaMallocAsync_ptsz__res")));
static void* cudaMallocAsync_ptsz__res(void) { return _sym("cudaMallocAsync_ptsz"); }
void* cudaMallocFromPoolAsync_ptsz() __attribute__((ifunc("cudaMallocFromPoolAsync_ptsz__res")));
static void* cudaMallocFromPoolAsync_ptsz__res(void) { return _sym("cudaMallocFromPoolAsync_ptsz"); }
void* cudaMallocHost() __attribute__((ifunc("cudaMallocHost__res")));
static void* cudaMallocHost__res(void) { return _sym("cudaMallocHost"); }
void* cudaMallocMipmappedArray() __attribute__((ifunc("cudaMallocMipmappedArray__res")));
static void* cudaMallocMipmappedArray__res(void) { return _sym("cudaMallocMipmappedArray"); }
void* cudaMallocPitch() __attribute__((ifunc("cudaMallocPitch__res")));
static void* cudaMallocPitch__res(void) { return _sym("cudaMallocPitch"); }
void* cudaMemAdvise() __attribute__((ifunc("cudaMemAdvise__res")));
static void* cudaMemAdvise__res(void) { return _sym("cudaMemAdvise"); }
void* cudaMemAdvise_v2() __attribute__((ifunc("cudaMemAdvise_v2__res")));
static void* cudaMemAdvise_v2__res(void) { return _sym("cudaMemAdvise_v2"); }
void* cudaMemcpy() __attribute__((ifunc("cudaMemcpy__res")));
static void* cudaMemcpy__res(void) { return _sym("cudaMemcpy"); }
void* cudaMemcpy2D() __attribute__((ifunc("cudaMemcpy2D__res")));
static void* cudaMemcpy2D__res(void) { return _sym("cudaMemcpy2D"); }
void* cudaMemcpy2DArrayToArray() __attribute__((ifunc("cudaMemcpy2DArrayToArray__res")));
static void* cudaMemcpy2DArrayToArray__res(void) { return _sym("cudaMemcpy2DArrayToArray"); }
void* cudaMemcpy2DArrayToArray_ptds() __attribute__((ifunc("cudaMemcpy2DArrayToArray_ptds__res")));
static void* cudaMemcpy2DArrayToArray_ptds__res(void) { return _sym("cudaMemcpy2DArrayToArray_ptds"); }
void* cudaMemcpy2DAsync() __attribute__((ifunc("cudaMemcpy2DAsync__res")));
static void* cudaMemcpy2DAsync__res(void) { return _sym("cudaMemcpy2DAsync"); }
void* cudaMemcpy2DAsync_ptsz() __attribute__((ifunc("cudaMemcpy2DAsync_ptsz__res")));
static void* cudaMemcpy2DAsync_ptsz__res(void) { return _sym("cudaMemcpy2DAsync_ptsz"); }
void* cudaMemcpy2DFromArray() __attribute__((ifunc("cudaMemcpy2DFromArray__res")));
static void* cudaMemcpy2DFromArray__res(void) { return _sym("cudaMemcpy2DFromArray"); }
void* cudaMemcpy2DFromArrayAsync() __attribute__((ifunc("cudaMemcpy2DFromArrayAsync__res")));
static void* cudaMemcpy2DFromArrayAsync__res(void) { return _sym("cudaMemcpy2DFromArrayAsync"); }
void* cudaMemcpy2DFromArrayAsync_ptsz() __attribute__((ifunc("cudaMemcpy2DFromArrayAsync_ptsz__res")));
static void* cudaMemcpy2DFromArrayAsync_ptsz__res(void) { return _sym("cudaMemcpy2DFromArrayAsync_ptsz"); }
void* cudaMemcpy2DFromArray_ptds() __attribute__((ifunc("cudaMemcpy2DFromArray_ptds__res")));
static void* cudaMemcpy2DFromArray_ptds__res(void) { return _sym("cudaMemcpy2DFromArray_ptds"); }
void* cudaMemcpy2D_ptds() __attribute__((ifunc("cudaMemcpy2D_ptds__res")));
static void* cudaMemcpy2D_ptds__res(void) { return _sym("cudaMemcpy2D_ptds"); }
void* cudaMemcpy2DToArray() __attribute__((ifunc("cudaMemcpy2DToArray__res")));
static void* cudaMemcpy2DToArray__res(void) { return _sym("cudaMemcpy2DToArray"); }
void* cudaMemcpy2DToArrayAsync() __attribute__((ifunc("cudaMemcpy2DToArrayAsync__res")));
static void* cudaMemcpy2DToArrayAsync__res(void) { return _sym("cudaMemcpy2DToArrayAsync"); }
void* cudaMemcpy2DToArrayAsync_ptsz() __attribute__((ifunc("cudaMemcpy2DToArrayAsync_ptsz__res")));
static void* cudaMemcpy2DToArrayAsync_ptsz__res(void) { return _sym("cudaMemcpy2DToArrayAsync_ptsz"); }
void* cudaMemcpy2DToArray_ptds() __attribute__((ifunc("cudaMemcpy2DToArray_ptds__res")));
static void* cudaMemcpy2DToArray_ptds__res(void) { return _sym("cudaMemcpy2DToArray_ptds"); }
void* cudaMemcpy3D() __attribute__((ifunc("cudaMemcpy3D__res")));
static void* cudaMemcpy3D__res(void) { return _sym("cudaMemcpy3D"); }
void* cudaMemcpy3DAsync() __attribute__((ifunc("cudaMemcpy3DAsync__res")));
static void* cudaMemcpy3DAsync__res(void) { return _sym("cudaMemcpy3DAsync"); }
void* cudaMemcpy3DAsync_ptsz() __attribute__((ifunc("cudaMemcpy3DAsync_ptsz__res")));
static void* cudaMemcpy3DAsync_ptsz__res(void) { return _sym("cudaMemcpy3DAsync_ptsz"); }
void* cudaMemcpy3DPeer() __attribute__((ifunc("cudaMemcpy3DPeer__res")));
static void* cudaMemcpy3DPeer__res(void) { return _sym("cudaMemcpy3DPeer"); }
void* cudaMemcpy3DPeerAsync() __attribute__((ifunc("cudaMemcpy3DPeerAsync__res")));
static void* cudaMemcpy3DPeerAsync__res(void) { return _sym("cudaMemcpy3DPeerAsync"); }
void* cudaMemcpy3DPeerAsync_ptsz() __attribute__((ifunc("cudaMemcpy3DPeerAsync_ptsz__res")));
static void* cudaMemcpy3DPeerAsync_ptsz__res(void) { return _sym("cudaMemcpy3DPeerAsync_ptsz"); }
void* cudaMemcpy3DPeer_ptds() __attribute__((ifunc("cudaMemcpy3DPeer_ptds__res")));
static void* cudaMemcpy3DPeer_ptds__res(void) { return _sym("cudaMemcpy3DPeer_ptds"); }
void* cudaMemcpy3D_ptds() __attribute__((ifunc("cudaMemcpy3D_ptds__res")));
static void* cudaMemcpy3D_ptds__res(void) { return _sym("cudaMemcpy3D_ptds"); }
void* cudaMemcpyArrayToArray() __attribute__((ifunc("cudaMemcpyArrayToArray__res")));
static void* cudaMemcpyArrayToArray__res(void) { return _sym("cudaMemcpyArrayToArray"); }
void* cudaMemcpyArrayToArray_ptds() __attribute__((ifunc("cudaMemcpyArrayToArray_ptds__res")));
static void* cudaMemcpyArrayToArray_ptds__res(void) { return _sym("cudaMemcpyArrayToArray_ptds"); }
void* cudaMemcpyAsync() __attribute__((ifunc("cudaMemcpyAsync__res")));
static void* cudaMemcpyAsync__res(void) { return _sym("cudaMemcpyAsync"); }
void* cudaMemcpyAsync_ptsz() __attribute__((ifunc("cudaMemcpyAsync_ptsz__res")));
static void* cudaMemcpyAsync_ptsz__res(void) { return _sym("cudaMemcpyAsync_ptsz"); }
void* cudaMemcpyFromArray() __attribute__((ifunc("cudaMemcpyFromArray__res")));
static void* cudaMemcpyFromArray__res(void) { return _sym("cudaMemcpyFromArray"); }
void* cudaMemcpyFromArrayAsync() __attribute__((ifunc("cudaMemcpyFromArrayAsync__res")));
static void* cudaMemcpyFromArrayAsync__res(void) { return _sym("cudaMemcpyFromArrayAsync"); }
void* cudaMemcpyFromArrayAsync_ptsz() __attribute__((ifunc("cudaMemcpyFromArrayAsync_ptsz__res")));
static void* cudaMemcpyFromArrayAsync_ptsz__res(void) { return _sym("cudaMemcpyFromArrayAsync_ptsz"); }
void* cudaMemcpyFromArray_ptds() __attribute__((ifunc("cudaMemcpyFromArray_ptds__res")));
static void* cudaMemcpyFromArray_ptds__res(void) { return _sym("cudaMemcpyFromArray_ptds"); }
void* cudaMemcpyFromSymbol() __attribute__((ifunc("cudaMemcpyFromSymbol__res")));
static void* cudaMemcpyFromSymbol__res(void) { return _sym("cudaMemcpyFromSymbol"); }
void* cudaMemcpyFromSymbolAsync() __attribute__((ifunc("cudaMemcpyFromSymbolAsync__res")));
static void* cudaMemcpyFromSymbolAsync__res(void) { return _sym("cudaMemcpyFromSymbolAsync"); }
void* cudaMemcpyFromSymbolAsync_ptsz() __attribute__((ifunc("cudaMemcpyFromSymbolAsync_ptsz__res")));
static void* cudaMemcpyFromSymbolAsync_ptsz__res(void) { return _sym("cudaMemcpyFromSymbolAsync_ptsz"); }
void* cudaMemcpyFromSymbol_ptds() __attribute__((ifunc("cudaMemcpyFromSymbol_ptds__res")));
static void* cudaMemcpyFromSymbol_ptds__res(void) { return _sym("cudaMemcpyFromSymbol_ptds"); }
void* cudaMemcpyPeer() __attribute__((ifunc("cudaMemcpyPeer__res")));
static void* cudaMemcpyPeer__res(void) { return _sym("cudaMemcpyPeer"); }
void* cudaMemcpyPeerAsync() __attribute__((ifunc("cudaMemcpyPeerAsync__res")));
static void* cudaMemcpyPeerAsync__res(void) { return _sym("cudaMemcpyPeerAsync"); }
void* cudaMemcpy_ptds() __attribute__((ifunc("cudaMemcpy_ptds__res")));
static void* cudaMemcpy_ptds__res(void) { return _sym("cudaMemcpy_ptds"); }
void* cudaMemcpyToArray() __attribute__((ifunc("cudaMemcpyToArray__res")));
static void* cudaMemcpyToArray__res(void) { return _sym("cudaMemcpyToArray"); }
void* cudaMemcpyToArrayAsync() __attribute__((ifunc("cudaMemcpyToArrayAsync__res")));
static void* cudaMemcpyToArrayAsync__res(void) { return _sym("cudaMemcpyToArrayAsync"); }
void* cudaMemcpyToArrayAsync_ptsz() __attribute__((ifunc("cudaMemcpyToArrayAsync_ptsz__res")));
static void* cudaMemcpyToArrayAsync_ptsz__res(void) { return _sym("cudaMemcpyToArrayAsync_ptsz"); }
void* cudaMemcpyToArray_ptds() __attribute__((ifunc("cudaMemcpyToArray_ptds__res")));
static void* cudaMemcpyToArray_ptds__res(void) { return _sym("cudaMemcpyToArray_ptds"); }
void* cudaMemcpyToSymbol() __attribute__((ifunc("cudaMemcpyToSymbol__res")));
static void* cudaMemcpyToSymbol__res(void) { return _sym("cudaMemcpyToSymbol"); }
void* cudaMemcpyToSymbolAsync() __attribute__((ifunc("cudaMemcpyToSymbolAsync__res")));
static void* cudaMemcpyToSymbolAsync__res(void) { return _sym("cudaMemcpyToSymbolAsync"); }
void* cudaMemcpyToSymbolAsync_ptsz() __attribute__((ifunc("cudaMemcpyToSymbolAsync_ptsz__res")));
static void* cudaMemcpyToSymbolAsync_ptsz__res(void) { return _sym("cudaMemcpyToSymbolAsync_ptsz"); }
void* cudaMemcpyToSymbol_ptds() __attribute__((ifunc("cudaMemcpyToSymbol_ptds__res")));
static void* cudaMemcpyToSymbol_ptds__res(void) { return _sym("cudaMemcpyToSymbol_ptds"); }
void* cudaMemPoolCreate() __attribute__((ifunc("cudaMemPoolCreate__res")));
static void* cudaMemPoolCreate__res(void) { return _sym("cudaMemPoolCreate"); }
void* cudaMemPoolDestroy() __attribute__((ifunc("cudaMemPoolDestroy__res")));
static void* cudaMemPoolDestroy__res(void) { return _sym("cudaMemPoolDestroy"); }
void* cudaMemPoolExportPointer() __attribute__((ifunc("cudaMemPoolExportPointer__res")));
static void* cudaMemPoolExportPointer__res(void) { return _sym("cudaMemPoolExportPointer"); }
void* cudaMemPoolExportToShareableHandle() __attribute__((ifunc("cudaMemPoolExportToShareableHandle__res")));
static void* cudaMemPoolExportToShareableHandle__res(void) { return _sym("cudaMemPoolExportToShareableHandle"); }
void* cudaMemPoolGetAccess() __attribute__((ifunc("cudaMemPoolGetAccess__res")));
static void* cudaMemPoolGetAccess__res(void) { return _sym("cudaMemPoolGetAccess"); }
void* cudaMemPoolGetAttribute() __attribute__((ifunc("cudaMemPoolGetAttribute__res")));
static void* cudaMemPoolGetAttribute__res(void) { return _sym("cudaMemPoolGetAttribute"); }
void* cudaMemPoolImportFromShareableHandle() __attribute__((ifunc("cudaMemPoolImportFromShareableHandle__res")));
static void* cudaMemPoolImportFromShareableHandle__res(void) { return _sym("cudaMemPoolImportFromShareableHandle"); }
void* cudaMemPoolImportPointer() __attribute__((ifunc("cudaMemPoolImportPointer__res")));
static void* cudaMemPoolImportPointer__res(void) { return _sym("cudaMemPoolImportPointer"); }
void* cudaMemPoolSetAccess() __attribute__((ifunc("cudaMemPoolSetAccess__res")));
static void* cudaMemPoolSetAccess__res(void) { return _sym("cudaMemPoolSetAccess"); }
void* cudaMemPoolSetAttribute() __attribute__((ifunc("cudaMemPoolSetAttribute__res")));
static void* cudaMemPoolSetAttribute__res(void) { return _sym("cudaMemPoolSetAttribute"); }
void* cudaMemPoolTrimTo() __attribute__((ifunc("cudaMemPoolTrimTo__res")));
static void* cudaMemPoolTrimTo__res(void) { return _sym("cudaMemPoolTrimTo"); }
void* cudaMemPrefetchAsync() __attribute__((ifunc("cudaMemPrefetchAsync__res")));
static void* cudaMemPrefetchAsync__res(void) { return _sym("cudaMemPrefetchAsync"); }
void* cudaMemPrefetchAsync_ptsz() __attribute__((ifunc("cudaMemPrefetchAsync_ptsz__res")));
static void* cudaMemPrefetchAsync_ptsz__res(void) { return _sym("cudaMemPrefetchAsync_ptsz"); }
void* cudaMemPrefetchAsync_v2() __attribute__((ifunc("cudaMemPrefetchAsync_v2__res")));
static void* cudaMemPrefetchAsync_v2__res(void) { return _sym("cudaMemPrefetchAsync_v2"); }
void* cudaMemPrefetchAsync_v2_ptsz() __attribute__((ifunc("cudaMemPrefetchAsync_v2_ptsz__res")));
static void* cudaMemPrefetchAsync_v2_ptsz__res(void) { return _sym("cudaMemPrefetchAsync_v2_ptsz"); }
void* cudaMemRangeGetAttribute() __attribute__((ifunc("cudaMemRangeGetAttribute__res")));
static void* cudaMemRangeGetAttribute__res(void) { return _sym("cudaMemRangeGetAttribute"); }
void* cudaMemRangeGetAttributes() __attribute__((ifunc("cudaMemRangeGetAttributes__res")));
static void* cudaMemRangeGetAttributes__res(void) { return _sym("cudaMemRangeGetAttributes"); }
void* cudaMemset() __attribute__((ifunc("cudaMemset__res")));
static void* cudaMemset__res(void) { return _sym("cudaMemset"); }
void* cudaMemset2D() __attribute__((ifunc("cudaMemset2D__res")));
static void* cudaMemset2D__res(void) { return _sym("cudaMemset2D"); }
void* cudaMemset2DAsync() __attribute__((ifunc("cudaMemset2DAsync__res")));
static void* cudaMemset2DAsync__res(void) { return _sym("cudaMemset2DAsync"); }
void* cudaMemset2DAsync_ptsz() __attribute__((ifunc("cudaMemset2DAsync_ptsz__res")));
static void* cudaMemset2DAsync_ptsz__res(void) { return _sym("cudaMemset2DAsync_ptsz"); }
void* cudaMemset2D_ptds() __attribute__((ifunc("cudaMemset2D_ptds__res")));
static void* cudaMemset2D_ptds__res(void) { return _sym("cudaMemset2D_ptds"); }
void* cudaMemset3D() __attribute__((ifunc("cudaMemset3D__res")));
static void* cudaMemset3D__res(void) { return _sym("cudaMemset3D"); }
void* cudaMemset3DAsync() __attribute__((ifunc("cudaMemset3DAsync__res")));
static void* cudaMemset3DAsync__res(void) { return _sym("cudaMemset3DAsync"); }
void* cudaMemset3DAsync_ptsz() __attribute__((ifunc("cudaMemset3DAsync_ptsz__res")));
static void* cudaMemset3DAsync_ptsz__res(void) { return _sym("cudaMemset3DAsync_ptsz"); }
void* cudaMemset3D_ptds() __attribute__((ifunc("cudaMemset3D_ptds__res")));
static void* cudaMemset3D_ptds__res(void) { return _sym("cudaMemset3D_ptds"); }
void* cudaMemsetAsync() __attribute__((ifunc("cudaMemsetAsync__res")));
static void* cudaMemsetAsync__res(void) { return _sym("cudaMemsetAsync"); }
void* cudaMemsetAsync_ptsz() __attribute__((ifunc("cudaMemsetAsync_ptsz__res")));
static void* cudaMemsetAsync_ptsz__res(void) { return _sym("cudaMemsetAsync_ptsz"); }
void* cudaMemset_ptds() __attribute__((ifunc("cudaMemset_ptds__res")));
static void* cudaMemset_ptds__res(void) { return _sym("cudaMemset_ptds"); }
void* cudaMipmappedArrayGetMemoryRequirements() __attribute__((ifunc("cudaMipmappedArrayGetMemoryRequirements__res")));
static void* cudaMipmappedArrayGetMemoryRequirements__res(void) { return _sym("cudaMipmappedArrayGetMemoryRequirements"); }
void* cudaMipmappedArrayGetSparseProperties() __attribute__((ifunc("cudaMipmappedArrayGetSparseProperties__res")));
static void* cudaMipmappedArrayGetSparseProperties__res(void) { return _sym("cudaMipmappedArrayGetSparseProperties"); }
void* cudaOccupancyAvailableDynamicSMemPerBlock() __attribute__((ifunc("cudaOccupancyAvailableDynamicSMemPerBlock__res")));
static void* cudaOccupancyAvailableDynamicSMemPerBlock__res(void) { return _sym("cudaOccupancyAvailableDynamicSMemPerBlock"); }
void* cudaOccupancyMaxActiveBlocksPerMultiprocessor() __attribute__((ifunc("cudaOccupancyMaxActiveBlocksPerMultiprocessor__res")));
static void* cudaOccupancyMaxActiveBlocksPerMultiprocessor__res(void) { return _sym("cudaOccupancyMaxActiveBlocksPerMultiprocessor"); }
void* cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags() __attribute__((ifunc("cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags__res")));
static void* cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags__res(void) { return _sym("cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags"); }
void* cudaOccupancyMaxActiveClusters() __attribute__((ifunc("cudaOccupancyMaxActiveClusters__res")));
static void* cudaOccupancyMaxActiveClusters__res(void) { return _sym("cudaOccupancyMaxActiveClusters"); }
void* cudaOccupancyMaxPotentialClusterSize() __attribute__((ifunc("cudaOccupancyMaxPotentialClusterSize__res")));
static void* cudaOccupancyMaxPotentialClusterSize__res(void) { return _sym("cudaOccupancyMaxPotentialClusterSize"); }
void* cudaPeekAtLastError() __attribute__((ifunc("cudaPeekAtLastError__res")));
static void* cudaPeekAtLastError__res(void) { return _sym("cudaPeekAtLastError"); }
void* cudaPointerGetAttributes() __attribute__((ifunc("cudaPointerGetAttributes__res")));
static void* cudaPointerGetAttributes__res(void) { return _sym("cudaPointerGetAttributes"); }
void* __cudaPopCallConfiguration() __attribute__((ifunc("__cudaPopCallConfiguration__res")));
static void* __cudaPopCallConfiguration__res(void) { return _sym("__cudaPopCallConfiguration"); }
void* cudaProfilerStart() __attribute__((ifunc("cudaProfilerStart__res")));
static void* cudaProfilerStart__res(void) { return _sym("cudaProfilerStart"); }
void* cudaProfilerStop() __attribute__((ifunc("cudaProfilerStop__res")));
static void* cudaProfilerStop__res(void) { return _sym("cudaProfilerStop"); }
void* __cudaPushCallConfiguration() __attribute__((ifunc("__cudaPushCallConfiguration__res")));
static void* __cudaPushCallConfiguration__res(void) { return _sym("__cudaPushCallConfiguration"); }
void* __cudaRegisterFatBinary() __attribute__((ifunc("__cudaRegisterFatBinary__res")));
static void* __cudaRegisterFatBinary__res(void) { return _sym("__cudaRegisterFatBinary"); }
void* __cudaRegisterFatBinaryEnd() __attribute__((ifunc("__cudaRegisterFatBinaryEnd__res")));
static void* __cudaRegisterFatBinaryEnd__res(void) { return _sym("__cudaRegisterFatBinaryEnd"); }
void* __cudaRegisterFunction() __attribute__((ifunc("__cudaRegisterFunction__res")));
static void* __cudaRegisterFunction__res(void) { return _sym("__cudaRegisterFunction"); }
void* __cudaRegisterHostVar() __attribute__((ifunc("__cudaRegisterHostVar__res")));
static void* __cudaRegisterHostVar__res(void) { return _sym("__cudaRegisterHostVar"); }
void* __cudaRegisterManagedVar() __attribute__((ifunc("__cudaRegisterManagedVar__res")));
static void* __cudaRegisterManagedVar__res(void) { return _sym("__cudaRegisterManagedVar"); }
void* __cudaRegisterUnifiedTable() __attribute__((ifunc("__cudaRegisterUnifiedTable__res")));
static void* __cudaRegisterUnifiedTable__res(void) { return _sym("__cudaRegisterUnifiedTable"); }
void* __cudaRegisterVar() __attribute__((ifunc("__cudaRegisterVar__res")));
static void* __cudaRegisterVar__res(void) { return _sym("__cudaRegisterVar"); }
void* cudaRuntimeGetVersion() __attribute__((ifunc("cudaRuntimeGetVersion__res")));
static void* cudaRuntimeGetVersion__res(void) { return _sym("cudaRuntimeGetVersion"); }
void* cudaSetDevice() __attribute__((ifunc("cudaSetDevice__res")));
static void* cudaSetDevice__res(void) { return _sym("cudaSetDevice"); }
void* cudaSetDeviceFlags() __attribute__((ifunc("cudaSetDeviceFlags__res")));
static void* cudaSetDeviceFlags__res(void) { return _sym("cudaSetDeviceFlags"); }
void* cudaSetDoubleForDevice() __attribute__((ifunc("cudaSetDoubleForDevice__res")));
static void* cudaSetDoubleForDevice__res(void) { return _sym("cudaSetDoubleForDevice"); }
void* cudaSetDoubleForHost() __attribute__((ifunc("cudaSetDoubleForHost__res")));
static void* cudaSetDoubleForHost__res(void) { return _sym("cudaSetDoubleForHost"); }
void* cudaSetValidDevices() __attribute__((ifunc("cudaSetValidDevices__res")));
static void* cudaSetValidDevices__res(void) { return _sym("cudaSetValidDevices"); }
void* cudaSignalExternalSemaphoresAsync() __attribute__((ifunc("cudaSignalExternalSemaphoresAsync__res")));
static void* cudaSignalExternalSemaphoresAsync__res(void) { return _sym("cudaSignalExternalSemaphoresAsync"); }
void* cudaSignalExternalSemaphoresAsync_ptsz() __attribute__((ifunc("cudaSignalExternalSemaphoresAsync_ptsz__res")));
static void* cudaSignalExternalSemaphoresAsync_ptsz__res(void) { return _sym("cudaSignalExternalSemaphoresAsync_ptsz"); }
void* cudaSignalExternalSemaphoresAsync_v2() __attribute__((ifunc("cudaSignalExternalSemaphoresAsync_v2__res")));
static void* cudaSignalExternalSemaphoresAsync_v2__res(void) { return _sym("cudaSignalExternalSemaphoresAsync_v2"); }
void* cudaSignalExternalSemaphoresAsync_v2_ptsz() __attribute__((ifunc("cudaSignalExternalSemaphoresAsync_v2_ptsz__res")));
static void* cudaSignalExternalSemaphoresAsync_v2_ptsz__res(void) { return _sym("cudaSignalExternalSemaphoresAsync_v2_ptsz"); }
void* cudaStreamAddCallback() __attribute__((ifunc("cudaStreamAddCallback__res")));
static void* cudaStreamAddCallback__res(void) { return _sym("cudaStreamAddCallback"); }
void* cudaStreamAddCallback_ptsz() __attribute__((ifunc("cudaStreamAddCallback_ptsz__res")));
static void* cudaStreamAddCallback_ptsz__res(void) { return _sym("cudaStreamAddCallback_ptsz"); }
void* cudaStreamAttachMemAsync() __attribute__((ifunc("cudaStreamAttachMemAsync__res")));
static void* cudaStreamAttachMemAsync__res(void) { return _sym("cudaStreamAttachMemAsync"); }
void* cudaStreamAttachMemAsync_ptsz() __attribute__((ifunc("cudaStreamAttachMemAsync_ptsz__res")));
static void* cudaStreamAttachMemAsync_ptsz__res(void) { return _sym("cudaStreamAttachMemAsync_ptsz"); }
void* cudaStreamBeginCapture() __attribute__((ifunc("cudaStreamBeginCapture__res")));
static void* cudaStreamBeginCapture__res(void) { return _sym("cudaStreamBeginCapture"); }
void* cudaStreamBeginCapture_ptsz() __attribute__((ifunc("cudaStreamBeginCapture_ptsz__res")));
static void* cudaStreamBeginCapture_ptsz__res(void) { return _sym("cudaStreamBeginCapture_ptsz"); }
void* cudaStreamBeginCaptureToGraph() __attribute__((ifunc("cudaStreamBeginCaptureToGraph__res")));
static void* cudaStreamBeginCaptureToGraph__res(void) { return _sym("cudaStreamBeginCaptureToGraph"); }
void* cudaStreamBeginCaptureToGraph_ptsz() __attribute__((ifunc("cudaStreamBeginCaptureToGraph_ptsz__res")));
static void* cudaStreamBeginCaptureToGraph_ptsz__res(void) { return _sym("cudaStreamBeginCaptureToGraph_ptsz"); }
void* cudaStreamCopyAttributes() __attribute__((ifunc("cudaStreamCopyAttributes__res")));
static void* cudaStreamCopyAttributes__res(void) { return _sym("cudaStreamCopyAttributes"); }
void* cudaStreamCopyAttributes_ptsz() __attribute__((ifunc("cudaStreamCopyAttributes_ptsz__res")));
static void* cudaStreamCopyAttributes_ptsz__res(void) { return _sym("cudaStreamCopyAttributes_ptsz"); }
void* cudaStreamCreate() __attribute__((ifunc("cudaStreamCreate__res")));
static void* cudaStreamCreate__res(void) { return _sym("cudaStreamCreate"); }
void* cudaStreamCreateWithFlags() __attribute__((ifunc("cudaStreamCreateWithFlags__res")));
static void* cudaStreamCreateWithFlags__res(void) { return _sym("cudaStreamCreateWithFlags"); }
void* cudaStreamCreateWithPriority() __attribute__((ifunc("cudaStreamCreateWithPriority__res")));
static void* cudaStreamCreateWithPriority__res(void) { return _sym("cudaStreamCreateWithPriority"); }
void* cudaStreamDestroy() __attribute__((ifunc("cudaStreamDestroy__res")));
static void* cudaStreamDestroy__res(void) { return _sym("cudaStreamDestroy"); }
void* cudaStreamEndCapture() __attribute__((ifunc("cudaStreamEndCapture__res")));
static void* cudaStreamEndCapture__res(void) { return _sym("cudaStreamEndCapture"); }
void* cudaStreamEndCapture_ptsz() __attribute__((ifunc("cudaStreamEndCapture_ptsz__res")));
static void* cudaStreamEndCapture_ptsz__res(void) { return _sym("cudaStreamEndCapture_ptsz"); }
void* cudaStreamGetAttribute() __attribute__((ifunc("cudaStreamGetAttribute__res")));
static void* cudaStreamGetAttribute__res(void) { return _sym("cudaStreamGetAttribute"); }
void* cudaStreamGetAttribute_ptsz() __attribute__((ifunc("cudaStreamGetAttribute_ptsz__res")));
static void* cudaStreamGetAttribute_ptsz__res(void) { return _sym("cudaStreamGetAttribute_ptsz"); }
void* cudaStreamGetCaptureInfo() __attribute__((ifunc("cudaStreamGetCaptureInfo__res")));
static void* cudaStreamGetCaptureInfo__res(void) { return _sym("cudaStreamGetCaptureInfo"); }
void* cudaStreamGetCaptureInfo_ptsz() __attribute__((ifunc("cudaStreamGetCaptureInfo_ptsz__res")));
static void* cudaStreamGetCaptureInfo_ptsz__res(void) { return _sym("cudaStreamGetCaptureInfo_ptsz"); }
void* cudaStreamGetCaptureInfo_v2() __attribute__((ifunc("cudaStreamGetCaptureInfo_v2__res")));
static void* cudaStreamGetCaptureInfo_v2__res(void) { return _sym("cudaStreamGetCaptureInfo_v2"); }
void* cudaStreamGetCaptureInfo_v2_ptsz() __attribute__((ifunc("cudaStreamGetCaptureInfo_v2_ptsz__res")));
static void* cudaStreamGetCaptureInfo_v2_ptsz__res(void) { return _sym("cudaStreamGetCaptureInfo_v2_ptsz"); }
void* cudaStreamGetCaptureInfo_v3() __attribute__((ifunc("cudaStreamGetCaptureInfo_v3__res")));
static void* cudaStreamGetCaptureInfo_v3__res(void) { return _sym("cudaStreamGetCaptureInfo_v3"); }
void* cudaStreamGetCaptureInfo_v3_ptsz() __attribute__((ifunc("cudaStreamGetCaptureInfo_v3_ptsz__res")));
static void* cudaStreamGetCaptureInfo_v3_ptsz__res(void) { return _sym("cudaStreamGetCaptureInfo_v3_ptsz"); }
void* cudaStreamGetFlags() __attribute__((ifunc("cudaStreamGetFlags__res")));
static void* cudaStreamGetFlags__res(void) { return _sym("cudaStreamGetFlags"); }
void* cudaStreamGetFlags_ptsz() __attribute__((ifunc("cudaStreamGetFlags_ptsz__res")));
static void* cudaStreamGetFlags_ptsz__res(void) { return _sym("cudaStreamGetFlags_ptsz"); }
void* cudaStreamGetId() __attribute__((ifunc("cudaStreamGetId__res")));
static void* cudaStreamGetId__res(void) { return _sym("cudaStreamGetId"); }
void* cudaStreamGetId_ptsz() __attribute__((ifunc("cudaStreamGetId_ptsz__res")));
static void* cudaStreamGetId_ptsz__res(void) { return _sym("cudaStreamGetId_ptsz"); }
void* cudaStreamGetPriority() __attribute__((ifunc("cudaStreamGetPriority__res")));
static void* cudaStreamGetPriority__res(void) { return _sym("cudaStreamGetPriority"); }
void* cudaStreamGetPriority_ptsz() __attribute__((ifunc("cudaStreamGetPriority_ptsz__res")));
static void* cudaStreamGetPriority_ptsz__res(void) { return _sym("cudaStreamGetPriority_ptsz"); }
void* cudaStreamIsCapturing() __attribute__((ifunc("cudaStreamIsCapturing__res")));
static void* cudaStreamIsCapturing__res(void) { return _sym("cudaStreamIsCapturing"); }
void* cudaStreamIsCapturing_ptsz() __attribute__((ifunc("cudaStreamIsCapturing_ptsz__res")));
static void* cudaStreamIsCapturing_ptsz__res(void) { return _sym("cudaStreamIsCapturing_ptsz"); }
void* cudaStreamQuery() __attribute__((ifunc("cudaStreamQuery__res")));
static void* cudaStreamQuery__res(void) { return _sym("cudaStreamQuery"); }
void* cudaStreamQuery_ptsz() __attribute__((ifunc("cudaStreamQuery_ptsz__res")));
static void* cudaStreamQuery_ptsz__res(void) { return _sym("cudaStreamQuery_ptsz"); }
void* cudaStreamSetAttribute() __attribute__((ifunc("cudaStreamSetAttribute__res")));
static void* cudaStreamSetAttribute__res(void) { return _sym("cudaStreamSetAttribute"); }
void* cudaStreamSetAttribute_ptsz() __attribute__((ifunc("cudaStreamSetAttribute_ptsz__res")));
static void* cudaStreamSetAttribute_ptsz__res(void) { return _sym("cudaStreamSetAttribute_ptsz"); }
void* cudaStreamSynchronize() __attribute__((ifunc("cudaStreamSynchronize__res")));
static void* cudaStreamSynchronize__res(void) { return _sym("cudaStreamSynchronize"); }
void* cudaStreamSynchronize_ptsz() __attribute__((ifunc("cudaStreamSynchronize_ptsz__res")));
static void* cudaStreamSynchronize_ptsz__res(void) { return _sym("cudaStreamSynchronize_ptsz"); }
void* cudaStreamUpdateCaptureDependencies() __attribute__((ifunc("cudaStreamUpdateCaptureDependencies__res")));
static void* cudaStreamUpdateCaptureDependencies__res(void) { return _sym("cudaStreamUpdateCaptureDependencies"); }
void* cudaStreamUpdateCaptureDependencies_ptsz() __attribute__((ifunc("cudaStreamUpdateCaptureDependencies_ptsz__res")));
static void* cudaStreamUpdateCaptureDependencies_ptsz__res(void) { return _sym("cudaStreamUpdateCaptureDependencies_ptsz"); }
void* cudaStreamUpdateCaptureDependencies_v2() __attribute__((ifunc("cudaStreamUpdateCaptureDependencies_v2__res")));
static void* cudaStreamUpdateCaptureDependencies_v2__res(void) { return _sym("cudaStreamUpdateCaptureDependencies_v2"); }
void* cudaStreamUpdateCaptureDependencies_v2_ptsz() __attribute__((ifunc("cudaStreamUpdateCaptureDependencies_v2_ptsz__res")));
static void* cudaStreamUpdateCaptureDependencies_v2_ptsz__res(void) { return _sym("cudaStreamUpdateCaptureDependencies_v2_ptsz"); }
void* cudaStreamWaitEvent() __attribute__((ifunc("cudaStreamWaitEvent__res")));
static void* cudaStreamWaitEvent__res(void) { return _sym("cudaStreamWaitEvent"); }
void* cudaStreamWaitEvent_ptsz() __attribute__((ifunc("cudaStreamWaitEvent_ptsz__res")));
static void* cudaStreamWaitEvent_ptsz__res(void) { return _sym("cudaStreamWaitEvent_ptsz"); }
void* cudaThreadExchangeStreamCaptureMode() __attribute__((ifunc("cudaThreadExchangeStreamCaptureMode__res")));
static void* cudaThreadExchangeStreamCaptureMode__res(void) { return _sym("cudaThreadExchangeStreamCaptureMode"); }
void* cudaThreadExit() __attribute__((ifunc("cudaThreadExit__res")));
static void* cudaThreadExit__res(void) { return _sym("cudaThreadExit"); }
void* cudaThreadGetCacheConfig() __attribute__((ifunc("cudaThreadGetCacheConfig__res")));
static void* cudaThreadGetCacheConfig__res(void) { return _sym("cudaThreadGetCacheConfig"); }
void* cudaThreadGetLimit() __attribute__((ifunc("cudaThreadGetLimit__res")));
static void* cudaThreadGetLimit__res(void) { return _sym("cudaThreadGetLimit"); }
void* cudaThreadSetCacheConfig() __attribute__((ifunc("cudaThreadSetCacheConfig__res")));
static void* cudaThreadSetCacheConfig__res(void) { return _sym("cudaThreadSetCacheConfig"); }
void* cudaThreadSetLimit() __attribute__((ifunc("cudaThreadSetLimit__res")));
static void* cudaThreadSetLimit__res(void) { return _sym("cudaThreadSetLimit"); }
void* cudaThreadSynchronize() __attribute__((ifunc("cudaThreadSynchronize__res")));
static void* cudaThreadSynchronize__res(void) { return _sym("cudaThreadSynchronize"); }
void* __cudaUnregisterFatBinary() __attribute__((ifunc("__cudaUnregisterFatBinary__res")));
static void* __cudaUnregisterFatBinary__res(void) { return _sym("__cudaUnregisterFatBinary"); }
void* cudaUserObjectCreate() __attribute__((ifunc("cudaUserObjectCreate__res")));
static void* cudaUserObjectCreate__res(void) { return _sym("cudaUserObjectCreate"); }
void* cudaUserObjectRelease() __attribute__((ifunc("cudaUserObjectRelease__res")));
static void* cudaUserObjectRelease__res(void) { return _sym("cudaUserObjectRelease"); }
void* cudaUserObjectRetain() __attribute__((ifunc("cudaUserObjectRetain__res")));
static void* cudaUserObjectRetain__res(void) { return _sym("cudaUserObjectRetain"); }
void* cudaVDPAUGetDevice() __attribute__((ifunc("cudaVDPAUGetDevice__res")));
static void* cudaVDPAUGetDevice__res(void) { return _sym("cudaVDPAUGetDevice"); }
void* cudaVDPAUSetVDPAUDevice() __attribute__((ifunc("cudaVDPAUSetVDPAUDevice__res")));
static void* cudaVDPAUSetVDPAUDevice__res(void) { return _sym("cudaVDPAUSetVDPAUDevice"); }
void* cudaWaitExternalSemaphoresAsync() __attribute__((ifunc("cudaWaitExternalSemaphoresAsync__res")));
static void* cudaWaitExternalSemaphoresAsync__res(void) { return _sym("cudaWaitExternalSemaphoresAsync"); }
void* cudaWaitExternalSemaphoresAsync_ptsz() __attribute__((ifunc("cudaWaitExternalSemaphoresAsync_ptsz__res")));
static void* cudaWaitExternalSemaphoresAsync_ptsz__res(void) { return _sym("cudaWaitExternalSemaphoresAsync_ptsz"); }
void* cudaWaitExternalSemaphoresAsync_v2() __attribute__((ifunc("cudaWaitExternalSemaphoresAsync_v2__res")));
static void* cudaWaitExternalSemaphoresAsync_v2__res(void) { return _sym("cudaWaitExternalSemaphoresAsync_v2"); }
void* cudaWaitExternalSemaphoresAsync_v2_ptsz() __attribute__((ifunc("cudaWaitExternalSemaphoresAsync_v2_ptsz__res")));
static void* cudaWaitExternalSemaphoresAsync_v2_ptsz__res(void) { return _sym("cudaWaitExternalSemaphoresAsync_v2_ptsz"); }
