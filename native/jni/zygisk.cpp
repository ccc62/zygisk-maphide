#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <vector>
#include <iostream>
#include <sys/mman.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>

#include "zygisk.hpp"
#include <lsplt.hpp>
#include <android/log.h>

#define LOG_TAG "MapHide"

#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGF(...) __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__)

static void hide_from_maps(std::vector<lsplt::MapInfo> &maps) {
    for (auto &info : maps) {
        LOGI("hide: %s\n", info.path.data());
        void *addr = reinterpret_cast<void *>(info.start);
        size_t size = info.end - info.start;
        void *copy = mmap(nullptr, size, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (!copy || copy == MAP_FAILED) {
            LOGE("mmap failed: %s\n", strerror(errno));
            continue;
        }
        if ((info.perms & PROT_READ) == 0) {
            mprotect(addr, size, PROT_READ);
        }
        memcpy(copy, addr, size);
        void *new_addr = mremap(copy, size, size, MREMAP_MAYMOVE | MREMAP_FIXED, addr);
        if (new_addr == MAP_FAILED) {
            LOGE("mremap failed: %s\n", strerror(errno));
            munmap(copy, size);
            continue;
        }
        mprotect(new_addr, size, info.perms);
        madvise(new_addr, size, MADV_DONTDUMP);
    }
}

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

class MapHide : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        uint32_t flags = api->getFlags();
        if ((flags & zygisk::PROCESS_ON_DENYLIST) && args->uid > 1000) {
            DoHide();
        }
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api;
    JNIEnv *env;

    void DoHide() {
        auto maps = lsplt::MapInfo::Scan();
        struct stat st;
        if (stat("/data", &st)) return;

        // 隐藏与 com.termux 相关的内存映射
        for (auto iter = maps.begin(); iter != maps.end();) {
            if (iter->path.find("com.termux") != std::string::npos) {
                LOGI("Hiding Termux related mapping: %s\n", iter->path.data());
                iter = maps.erase(iter);
            } else if (iter->dev != st.st_dev || 
                       !(iter->path.starts_with("/system/") ||
                         iter->path.starts_with("/vendor/") ||
                         iter->path.starts_with("/product/") ||
                         iter->path.starts_with("/system_ext/"))) {
                iter = maps.erase(iter);
            } else {
                ++iter;
            }
        }
        hide_from_maps(maps);
    }
};

static void companion_handler(int i) {
    return;
}

REGISTER_ZYGISK_MODULE(MapHide)
REGISTER_ZYGISK_COMPANION(companion_handler)
