/* Synthetic check for VK_LAYER_X4MP_discard: a submitted command buffer that clears a
   host-visible buffer must not execute while the fence still signals and the query
   read returns at once. Run with and without X4MP_VK_DISCARD=1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s = %d\n", #x, r_); return 1; } } while (0)

int main(void) {
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance;
    CHECK(vkCreateInstance(&ici, NULL, &instance));
    uint32_t n = 1;
    VkPhysicalDevice physical;
    vkEnumeratePhysicalDevices(instance, &n, &physical);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0,
                                   .queueCount = 1, .pQueuePriorities = &priority};
    VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .synchronization2 = VK_TRUE};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f13, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
    VkDevice device;
    CHECK(vkCreateDevice(physical, &dci, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 256,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer buffer;
    CHECK(vkCreateBuffer(device, &bci, NULL, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    uint32_t type = 0;
    for (; type < props.memoryTypeCount; ++type)
        if ((req.memoryTypeBits & (1u << type)) &&
            (props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) break;
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = type};
    VkDeviceMemory memory;
    CHECK(vkAllocateMemory(device, &mai, NULL, &memory));
    CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    unsigned *mapped;
    CHECK(vkMapMemory(device, memory, 0, 256, 0, (void **)&mapped));
    memset(mapped, 0, 256);

    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0};
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(device, &cai, &cmd));
    VkQueryPoolCreateInfo qpi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 1};
    VkQueryPool queries;
    CHECK(vkCreateQueryPool(device, &qpi, NULL, &queries));
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(cmd, &begin));
    vkCmdResetQueryPool(cmd, queries, 0, 1);
    vkCmdFillBuffer(cmd, buffer, 0, 256, 0xabcdabcd);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries, 0);
    CHECK(vkEndCommandBuffer(cmd));

    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(vkCreateFence(device, &fci, NULL, &fence));
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    CHECK(vkQueueSubmit(queue, 1, &si, fence));
    CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    uint64_t stamp = 7;
    CHECK(vkGetQueryPoolResults(device, queries, 0, 1, sizeof(stamp), &stamp, sizeof(stamp),
                                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    int executed = mapped[0] == 0xabcdabcdu;

    /* Submit2 path, again with a fence. */
    CHECK(vkResetFences(device, 1, &fence));
    memset(mapped, 0, 256);
    VkCommandBufferSubmitInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd};
    VkSubmitInfo2 si2 = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbi};
    PFN_vkQueueSubmit2 submit2 = (PFN_vkQueueSubmit2)vkGetDeviceProcAddr(device, "vkQueueSubmit2");
    CHECK(submit2(queue, 1, &si2, fence));
    CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    int executed2 = mapped[0] == 0xabcdabcdu;

    printf("fence signalled; submit executed=%d submit2 executed=%d timestamp=%llu\n",
           executed, executed2, (unsigned long long)stamp);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
