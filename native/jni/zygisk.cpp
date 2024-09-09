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
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        LOGI("Zygisk 模块已加载");
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // 获取应用的包名并进行匹配
        const char *packageName = args->packageName;
        if (strcmp(packageName, "com.aistra.hail") == 0) {
            LOGI("检测到 com.aistra.hail 启动，执行隐藏操作...");
            
            // 连接到 root 伴生进程，执行 root 操作
            int fd = api->connectCompanion();
            if (fd != -1) {
                LOGI("Root 伴生进程连接成功，执行 root 操作...");
                rootHideOperation(fd); // 伴生进程中执行的操作
                close(fd);
            }

            // 执行隐藏操作
            DoHide();
        }

        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
        LOGI("system_server 进程启动");
        // 创建伴生进程用于 root 权限操作
        int fd = api->connectCompanion();
        if (fd != -1) {
            LOGI("Root 伴生进程连接成功");
            close(fd);
        }
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    zygisk::Api *api;
    JNIEnv *env;

    // 执行隐藏操作
    void DoHide() {
        auto maps = lsplt::MapInfo::Scan();
        struct stat st;
        if (stat("/data", &st)) return;

        // 过滤掉不需要隐藏的文件路径
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

    // 隐藏指定内存映射
    static void hide_from_maps(std::vector<lsplt::MapInfo> maps) {
        for (auto &info : maps) {
            LOGI("隐藏: %s\n", info.path.data());
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

    // 伴生进程中的 root 操作
    static void rootHideOperation(int fd) {
        LOGI("伴生进程中执行 root 操作...");
        // 在这里添加与 root 权限相关的操作
        // 例如：挂载、重定向、文件操作等
        write(fd, "root operation", 14);  // 向 companion 进程发送信号（仅供示例）
    }
};

// 伴生进程处理函数
static void companion_handler(int fd) {
    LOGI("Root 伴生进程启动");
    // 在这里处理与 root 相关的操作
    char buffer[256];
    int n = read(fd, buffer, sizeof(buffer));  // 读取主进程传来的数据
    if (n > 0) {
        LOGI("伴生进程收到数据: %s", buffer);
        // 在这里可以根据收到的命令执行相应操作
    }
    close(fd);
}

REGISTER_ZYGISK_MODULE(MapHide)
REGISTER_ZYGISK_COMPANION(companion_handler)
