#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>
#include <cstdio>
extern "C" PFN_vkVoidFunction VKAPI_CALL xl_swiftshader_vkGetInstanceProcAddr(VkInstance,const char*);
int main(){auto get=xl_swiftshader_vkGetInstanceProcAddr;auto create=reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"));if(!create)return 1;
VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_1;VkInstanceCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;info.pApplicationInfo=&app;VkInstance instance{};auto result=create(&info,nullptr,&instance);if(result){std::printf("create %d\n",result);return 2;}
auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get(instance,"vkEnumeratePhysicalDevices"));auto props=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get(instance,"vkGetPhysicalDeviceProperties"));auto destroy=reinterpret_cast<PFN_vkDestroyInstance>(get(instance,"vkDestroyInstance"));uint32_t count=0;result=enumerate(instance,&count,nullptr);VkPhysicalDevice device{};if(!result&&count){count=1;result=enumerate(instance,&count,&device);VkPhysicalDeviceProperties properties{};props(device,&properties);std::printf("device=%s api=%u\n",properties.deviceName,properties.apiVersion);}destroy(instance,nullptr);return device?0:3;}
