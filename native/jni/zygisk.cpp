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

class MapHide : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        LOGI("Zygisk 模块已加载");
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        uint32_t flags = api->getFlags();
        if ((flags & zygisk::PROCESS_ON_DENYLIST) && args->uid > 1000) {
            LOGI("执行模块隐藏操作...");
            DoHide();
        }
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        LOGI("system_server 进程启动");
        int fd = api->connectCompanion();  // 创建伴生进程
        if (fd != -1) {
            close(fd);
            LOGI("Root 伴生进程连接成功");
        }
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api;
    JNIEnv *env;

    void DoHide() {
        auto maps = lsplt::MapInfo::Scan();
        struct stat st;
        if (stat("/data", &st)) return;
        // hide module file from maps
        // detection: https://github.com/vvb2060/MagiskDetector/blob/master/README_ZH.md
        // hide all maps with path is data partition but path is not /data/*
        for (auto iter = maps.begin(); iter != maps.end();) {
            if (iter->dev != st.st_dev ||
            (!(iter->path).starts_with("/system/") &&
             !(iter->path).starts_with("/vendor/") &&
             !(iter->path).starts_with("/product/") &&
             !(iter->path).starts_with("/system_ext/"))) {
                iter = maps.erase(iter);
            } else {
                ++iter;
            }
        }
        hide_from_maps(maps);
    }

    static void hide_from_maps(std::vector<lsplt::MapInfo> maps) {
        for (auto &info : maps) {
            LOGI("hide: %s\n", info.path.data());
            void *addr = reinterpret_cast<void *>(info.start);
            size_t size = info.end - info.start;
            void *copy = mmap(nullptr, size, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            if ((info.perms & PROT_READ) == 0) {
                mprotect(addr, size, PROT_READ);
            }
            memcpy(copy, addr, size);
            mremap(copy, size, size, MREMAP_MAYMOVE | MREMAP_FIXED, addr);
            mprotect(addr, size, info.perms);
        }
    }
};

// 伴生进程处理函数
static void companion_handler(int fd) {
    LOGI("Root 伴生进程启动");
    // 在这里添加 Root 权限操作
    // 可以处理高权限的操作，例如操作系统文件、隐藏模块等
    close(fd);
}

REGISTER_ZYGISK_MODULE(MapHide)
REGISTER_ZYGISK_COMPANION(companion_handler)
