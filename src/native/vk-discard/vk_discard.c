/*
 * VK_LAYER_X4MP_discard: a Vulkan layer for the headless Linux dedicated server.
 *
 * The server renders through Mesa lavapipe (CPU Vulkan) and nobody looks at the frames,
 * yet rasterisation used most of the machine and throttled the simulation. With
 * X4MP_VK_DISCARD=1 this layer submits every queue batch WITHOUT its command buffers,
 * keeping the same wait/signal semaphores and fence, so the engine's synchronisation is
 * unchanged while no draw, dispatch or copy executes. Query results are reported as zero
 * immediately (the queries never run, and a WAIT read would otherwise block forever).
 * Presentation passes through so the swapchain keeps cycling.
 *
 * Without X4MP_VK_DISCARD=1 every call passes through unchanged. The layer never touches
 * the game binary; it is loaded only when the operator enables it via the Vulkan loader
 * (VK_LAYER_PATH + VK_INSTANCE_LAYERS / VK_LOADER_LAYERS_ENABLE).
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#define EXPORT __attribute__((visibility("default")))
#define MAX_OBJECTS 32

typedef struct {
    void *key;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkDestroyInstance destroy;
} instance_entry;

typedef struct {
    void *key;
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkDestroyDevice destroy;
    PFN_vkQueueSubmit submit;
    PFN_vkQueueSubmit2 submit2;
    PFN_vkQueueSubmit2 submit2khr;
    PFN_vkGetQueryPoolResults query_results;
} device_entry;

static instance_entry instances[MAX_OBJECTS];
static device_entry devices[MAX_OBJECTS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_ullong discarded_buffers;
static atomic_ullong submits;
static int discard = -1;

/* The loader's dispatch pointer is the first word of every dispatchable handle; a queue
   shares its device's key. */
static void *key_of(const void *handle) { return *(void *const *)handle; }

static int discarding(void) {
    if (discard < 0) {
        const char *value = getenv("X4MP_VK_DISCARD");
        discard = value && strcmp(value, "1") == 0;
        fprintf(stderr, "X4MP_VK_DISCARD layer loaded: %s\n", discard ? "discarding command buffers" : "pass-through");
    }
    return discard;
}

static instance_entry *find_instance(void *key) {
    for (int i = 0; i < MAX_OBJECTS; ++i) if (instances[i].key == key) return &instances[i];
    return NULL;
}

static device_entry *find_device(void *key) {
    for (int i = 0; i < MAX_OBJECTS; ++i) if (devices[i].key == key) return &devices[i];
    return NULL;
}

static void report(void) {
    unsigned long long n = atomic_fetch_add(&submits, 1) + 1;
    if ((n & (n - 1)) == 0)  /* 1, 2, 4, 8, ... */
        fprintf(stderr, "X4MP_VK_DISCARD submits=%llu discarded_command_buffers=%llu\n", n,
                (unsigned long long)atomic_load(&discarded_buffers));
}

/* ---------------------------------------------------------------- queue submission */

static VKAPI_ATTR VkResult VKAPI_CALL layer_QueueSubmit(VkQueue queue, uint32_t count,
                                                        const VkSubmitInfo *submits_in, VkFence fence) {
    device_entry *d = find_device(key_of(queue));
    if (!d || !d->submit) return VK_ERROR_DEVICE_LOST;
    if (!discarding() || count == 0) return d->submit(queue, count, submits_in, fence);
    VkSubmitInfo stack[16] = {0};
    VkSubmitInfo *copy = count <= 16 ? stack : malloc(sizeof(*copy) * count);
    if (!copy) return VK_ERROR_OUT_OF_HOST_MEMORY;
    for (uint32_t i = 0; i < count; ++i) {
        copy[i] = submits_in[i];
        atomic_fetch_add(&discarded_buffers, copy[i].commandBufferCount);
        copy[i].commandBufferCount = 0;
        copy[i].pCommandBuffers = NULL;
    }
    VkResult result = d->submit(queue, count, copy, fence);
    if (copy != stack) free(copy);
    report();
    return result;
}

static VkResult submit2_common(PFN_vkQueueSubmit2 next, VkQueue queue, uint32_t count,
                               const VkSubmitInfo2 *submits_in, VkFence fence) {
    if (!next) return VK_ERROR_DEVICE_LOST;
    if (!discarding() || count == 0) return next(queue, count, submits_in, fence);
    VkSubmitInfo2 stack[16] = {0};
    VkSubmitInfo2 *copy = count <= 16 ? stack : malloc(sizeof(*copy) * count);
    if (!copy) return VK_ERROR_OUT_OF_HOST_MEMORY;
    for (uint32_t i = 0; i < count; ++i) {
        copy[i] = submits_in[i];
        atomic_fetch_add(&discarded_buffers, copy[i].commandBufferInfoCount);
        copy[i].commandBufferInfoCount = 0;
        copy[i].pCommandBufferInfos = NULL;
    }
    VkResult result = next(queue, count, copy, fence);
    if (copy != stack) free(copy);
    report();
    return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_QueueSubmit2(VkQueue queue, uint32_t count,
                                                         const VkSubmitInfo2 *submits_in, VkFence fence) {
    device_entry *d = find_device(key_of(queue));
    return submit2_common(d ? d->submit2 : NULL, queue, count, submits_in, fence);
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_QueueSubmit2KHR(VkQueue queue, uint32_t count,
                                                            const VkSubmitInfo2 *submits_in, VkFence fence) {
    device_entry *d = find_device(key_of(queue));
    return submit2_common(d ? d->submit2khr : NULL, queue, count, submits_in, fence);
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_GetQueryPoolResults(VkDevice device, VkQueryPool pool,
        uint32_t first, uint32_t count, size_t size, void *data, VkDeviceSize stride, VkQueryResultFlags flags) {
    device_entry *d = find_device(key_of(device));
    if (!d || !d->query_results) return VK_ERROR_DEVICE_LOST;
    if (!discarding()) return d->query_results(device, pool, first, count, size, data, stride, flags);
    /* Queries never execute: report zero results that are "available". */
    memset(data, 0, size);
    if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
        size_t word = (flags & VK_QUERY_RESULT_64_BIT) ? 8 : 4;
        for (uint32_t i = 0; i < count; ++i) {
            size_t offset = (size_t)stride * i;
            /* The availability word follows the result values; set the last word of each slot. */
            if (offset + word <= size && stride >= word) {
                unsigned char *slot = (unsigned char *)data + offset + (size_t)stride - word;
                if (word == 8) *(uint64_t *)slot = 1; else *(uint32_t *)slot = 1;
            }
        }
    }
    return VK_SUCCESS;
}

/* ---------------------------------------------------------------- device chain */

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_GetDeviceProcAddr(VkDevice device, const char *name);

static VKAPI_ATTR void VKAPI_CALL layer_DestroyDevice(VkDevice device, const VkAllocationCallbacks *allocator) {
    pthread_mutex_lock(&lock);
    device_entry *d = find_device(key_of(device));
    PFN_vkDestroyDevice destroy = d ? d->destroy : NULL;
    if (d) memset(d, 0, sizeof(*d));
    pthread_mutex_unlock(&lock);
    if (destroy) destroy(device, allocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo *info,
                                                         const VkAllocationCallbacks *allocator, VkDevice *out) {
    VkLayerDeviceCreateInfo *chain = (VkLayerDeviceCreateInfo *)info->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
        chain = (VkLayerDeviceCreateInfo *)chain->pNext;
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    PFN_vkCreateDevice create = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");
    VkResult result = create(physical, info, allocator, out);
    if (result != VK_SUCCESS) return result;
    pthread_mutex_lock(&lock);
    device_entry *d = find_device(NULL);
    if (d) {
        d->key = key_of(*out);
        d->gdpa = gdpa;
        d->destroy = (PFN_vkDestroyDevice)gdpa(*out, "vkDestroyDevice");
        d->submit = (PFN_vkQueueSubmit)gdpa(*out, "vkQueueSubmit");
        d->submit2 = (PFN_vkQueueSubmit2)gdpa(*out, "vkQueueSubmit2");
        d->submit2khr = (PFN_vkQueueSubmit2)gdpa(*out, "vkQueueSubmit2KHR");
        d->query_results = (PFN_vkGetQueryPoolResults)gdpa(*out, "vkGetQueryPoolResults");
    }
    pthread_mutex_unlock(&lock);
    if (!d) fprintf(stderr, "X4MP_VK_DISCARD: too many devices; device left unwrapped\n");
    discarding();
    return VK_SUCCESS;
}

/* ---------------------------------------------------------------- instance chain */

static VKAPI_ATTR void VKAPI_CALL layer_DestroyInstance(VkInstance instance, const VkAllocationCallbacks *allocator) {
    pthread_mutex_lock(&lock);
    instance_entry *e = find_instance(key_of(instance));
    PFN_vkDestroyInstance destroy = e ? e->destroy : NULL;
    if (e) memset(e, 0, sizeof(*e));
    pthread_mutex_unlock(&lock);
    if (destroy) destroy(instance, allocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL layer_CreateInstance(const VkInstanceCreateInfo *info,
                                                           const VkAllocationCallbacks *allocator, VkInstance *out) {
    VkLayerInstanceCreateInfo *chain = (VkLayerInstanceCreateInfo *)info->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
        chain = (VkLayerInstanceCreateInfo *)chain->pNext;
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance");
    VkResult result = create(info, allocator, out);
    if (result != VK_SUCCESS) return result;
    pthread_mutex_lock(&lock);
    instance_entry *e = find_instance(NULL);
    if (e) {
        e->key = key_of(*out);
        e->gipa = gipa;
        e->destroy = (PFN_vkDestroyInstance)gipa(*out, "vkDestroyInstance");
    }
    pthread_mutex_unlock(&lock);
    return e ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

#define HOOK(fn) if (!strcmp(name, "vk" #fn)) return (PFN_vkVoidFunction)layer_##fn

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_GetDeviceProcAddr(VkDevice device, const char *name) {
    HOOK(GetDeviceProcAddr);
    HOOK(DestroyDevice);
    HOOK(QueueSubmit);
    HOOK(QueueSubmit2);
    HOOK(QueueSubmit2KHR);
    HOOK(GetQueryPoolResults);
    device_entry *d = find_device(key_of(device));
    return d ? d->gdpa(device, name) : NULL;
}

/* Internal entry points: the exported names below can be preempted by libvulkan's own
   vkGetInstanceProcAddr/vkGetDeviceProcAddr in the global symbol scope, so the loader is
   always handed these static functions (the build also links with -Bsymbolic). */
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_GetInstanceProcAddr(VkInstance instance, const char *name) {
    HOOK(CreateInstance);
    HOOK(DestroyInstance);
    HOOK(CreateDevice);
    HOOK(GetInstanceProcAddr);
    HOOK(GetDeviceProcAddr);
    HOOK(DestroyDevice);
    HOOK(QueueSubmit);
    HOOK(QueueSubmit2);
    HOOK(QueueSubmit2KHR);
    HOOK(GetQueryPoolResults);
    if (!instance) return NULL;
    instance_entry *e = find_instance(key_of(instance));
    return e ? e->gipa(instance, name) : NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *name) {
    return layer_GetDeviceProcAddr(device, name);
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name) {
    return layer_GetInstanceProcAddr(instance, name);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *v) {
    if (v->loaderLayerInterfaceVersion > 2) v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = layer_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = layer_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
