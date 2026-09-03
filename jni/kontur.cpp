#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#define LOG_TAG "KonturMod"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// IL2CPP API function pointers
typedef void* (*il2cpp_domain_get_t)();
typedef void** (*il2cpp_domain_get_assemblies_t)(void* domain, size_t* size);
typedef void* (*il2cpp_assembly_get_image_t)(void* assembly);
typedef void* (*il2cpp_class_from_name_t)(void* image, const char* namespaze, const char* name);
typedef void* (*il2cpp_class_get_methods_t)(void* klass, void** iter);
typedef const char* (*il2cpp_method_get_name_t)(void* method);
typedef void* (*il2cpp_resolve_icall_t)(const char* name);

static il2cpp_domain_get_t il2cpp_domain_get;
static il2cpp_domain_get_assemblies_t il2cpp_domain_get_assemblies;
static il2cpp_assembly_get_image_t il2cpp_assembly_get_image;
static il2cpp_class_from_name_t il2cpp_class_from_name;
static il2cpp_class_get_methods_t il2cpp_class_get_methods;
static il2cpp_method_get_name_t il2cpp_method_get_name;

// Original UnityWebRequest.set_url function pointer
static void* original_set_url = nullptr;

// Replacement domain for URL interception
static const char* TARGET_DOMAIN = "konturinf.net";
static const char* REPLACEMENT_DOMAIN = "kntrmod.ru";

// Simple inline hook for ARM64 (B instruction - branch)
bool inline_hook_arm64(void* target, void* replacement, void** backup) {
    if (!target || !replacement) return false;
    
    // Calculate offset for branch instruction
    uintptr_t from = (uintptr_t)target;
    uintptr_t to = (uintptr_t)replacement;
    int64_t offset = ((int64_t)to - (int64_t)from) / 4;
    
    // Check if offset fits in 26 bits (B instruction limit: ±128MB)
    if (offset > 0x1FFFFFF || offset < -0x2000000) {
        LOGE("Hook offset too large: %lld", (long long)offset);
        return false;
    }
    
    // Save original bytes for backup
    if (backup) {
        *backup = malloc(16);
        memcpy(*backup, target, 16);
    }
    
    // Make page writable
    uintptr_t page = from & ~(getpagesize() - 1);
    mprotect((void*)page, getpagesize() * 2, PROT_READ | PROT_WRITE | PROT_EXEC);
    
    // Write B instruction (0x14000000 | (offset & 0x3FFFFFF))
    uint32_t branch_instr = 0x14000000 | (offset & 0x3FFFFFF);
    *(uint32_t*)target = branch_instr;
    
    // Clear instruction cache
    __builtin___clear_cache((char*)target, (char*)target + 4);
    
    // Restore page protection
    mprotect((void*)page, getpagesize() * 2, PROT_READ | PROT_EXEC);
    
    LOGD("Hooked %p -> %p (offset: %lld)", target, replacement, (long long)offset);
    return true;
}

// Hooked UnityWebRequest.set_url
// Signature: void UnityWebRequest::set_url(UnityWebRequest* this, Il2CppString* url)
typedef struct Il2CppString {
    void* klass;
    void* monitor;
    int32_t length;
    wchar_t chars[1];
} Il2CppString;

typedef void (*set_url_func)(void* thisPtr, Il2CppString* url);

static void* hooked_set_url_impl(void* thisPtr, Il2CppString* url) {
    if (url && url->length > 0) {
        // Convert wide string to char for logging
        char buffer[512] = {0};
        wcstombs(buffer, url->chars, sizeof(buffer) - 1);
        
        LOGD("UnityWebRequest.set_url called: %s", buffer);
        
        // Check if URL contains target domain
        if (wcsstr(url->chars, L"konturinf.net")) {
            LOGD("Intercepting konturinf.net -> kntrmod.ru");
            
            // Replace domain in-place
            wchar_t* pos = wcsstr(url->chars, L"konturinf.net");
            if (pos) {
                // Construct new URL
                wchar_t new_url[512] = {0};
                size_t prefix_len = pos - url->chars;
                wcsncpy(new_url, url->chars, prefix_len);
                wcscat(new_url, L"kntrmod.ru");
                wcscat(new_url, pos + wcslen(L"konturinf.net"));
                
                // Modify the Il2CppString in-place (risky but fast)
                wcscpy(url->chars, new_url);
                url->length = wcslen(new_url);
                
                wcstombs(buffer, new_url, sizeof(buffer) - 1);
                LOGD("Redirected to: %s", buffer);
            }
        }
    }
    
    // Call original
    if (original_set_url) {
        return ((set_url_func)original_set_url)(thisPtr, url);
    }
    
    return nullptr;
}

// ARM64 trampoline wrapper (needed because inline hook overwrites first instruction)
__attribute__((naked))
static void hooked_set_url() {
    __asm__ volatile(
        "stp x29, x30, [sp, #-16]!\n"
        "mov x29, sp\n"
        "bl hooked_set_url_impl\n"
        "ldp x29, x30, [sp], #16\n"
        "ret\n"
    );
}

// Find and hook UnityWebRequest.set_url
static bool hook_unity_web_request() {
    LOGD("Searching for UnityWebRequest.set_url...");
    
    void* domain = il2cpp_domain_get();
    if (!domain) {
        LOGE("Failed to get IL2CPP domain");
        return false;
    }
    
    size_t assembly_count = 0;
    void** assemblies = il2cpp_domain_get_assemblies(domain, &assembly_count);
    
    LOGD("Found %zu assemblies", assembly_count);
    
    for (size_t i = 0; i < assembly_count; i++) {
        void* image = il2cpp_assembly_get_image(assemblies[i]);
        if (!image) continue;
        
        // Look for UnityWebRequest in UnityEngine.UnityWebRequestModule
        void* klass = il2cpp_class_from_name(image, "UnityEngine.Networking", "UnityWebRequest");
        if (!klass) continue;
        
        LOGD("Found UnityWebRequest class");
        
        // Iterate methods to find set_url
        void* iter = nullptr;
        while (void* method = il2cpp_class_get_methods(klass, &iter)) {
            const char* method_name = il2cpp_method_get_name(method);
            if (method_name && strcmp(method_name, "set_url") == 0) {
                LOGD("Found set_url method at %p", method);
                
                // Hook it
                original_set_url = method;
                if (inline_hook_arm64(method, (void*)hooked_set_url, nullptr)) {
                    LOGD("Successfully hooked UnityWebRequest.set_url");
                    return true;
                } else {
                    LOGE("Failed to hook set_url");
                    return false;
                }
            }
        }
    }
    
    LOGE("UnityWebRequest.set_url not found");
    return false;
}

// Initialize IL2CPP API
static bool init_il2cpp() {
    LOGD("Loading libil2cpp.so...");
    
    void* handle = dlopen("libil2cpp.so", RTLD_LAZY);
    if (!handle) {
        LOGE("Failed to load libil2cpp.so: %s", dlerror());
        return false;
    }
    
    #define RESOLVE(name) \
        name = (name##_t)dlsym(handle, #name); \
        if (!name) { LOGE("Failed to resolve " #name); return false; }
    
    RESOLVE(il2cpp_domain_get);
    RESOLVE(il2cpp_domain_get_assemblies);
    RESOLVE(il2cpp_assembly_get_image);
    RESOLVE(il2cpp_class_from_name);
    RESOLVE(il2cpp_class_get_methods);
    RESOLVE(il2cpp_method_get_name);
    
    #undef RESOLVE
    
    LOGD("IL2CPP API initialized");
    return true;
}

// JNI entry point
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGD("=== KonturMod loaded ===");
    LOGD("Version: 1.0 minimalist");
    LOGD("Target: konturinf.net -> kntrmod.ru");
    
    // Wait for IL2CPP to be loaded by Unity
    sleep(2);
    
    if (!init_il2cpp()) {
        LOGE("Failed to initialize IL2CPP API");
        return JNI_VERSION_1_6;
    }
    
    if (!hook_unity_web_request()) {
        LOGE("Failed to hook UnityWebRequest");
        return JNI_VERSION_1_6;
    }
    
    LOGD("=== KonturMod initialization complete ===");
    return JNI_VERSION_1_6;
}
