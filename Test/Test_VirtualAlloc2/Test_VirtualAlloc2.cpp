#include <windows.h>
#include <stdio.h>
#include <vector>

// 辅助：把字节数转成人类易读的格式 (GB/TB)
void PrintFriendlySize(unsigned long long bytes) {
    double gb = (double)bytes / (1024 * 1024 * 1024);
    if (gb > 1024) {
        printf("%.4f TB", gb / 1024.0);
    }
    else {
        printf("%.4f GB", gb);
    }
}

int main() {
    // 0. 必须是 64 位环境，否则看不出效果
#if !defined(_WIN64)
    printf("错误：请在 x64 (64位) 模式下编译并运行此程序！\n");
    return -1;
#endif

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    printf("==============================================================\n");
    printf("              Windows 内存布局极限测试 (x64)                  \n");
    printf("==============================================================\n");
    printf("系统允许的最小地址 (地板): 0x%p\n", si.lpMinimumApplicationAddress);
    printf("系统允许的最大地址 (天花板): 0x%p\n", si.lpMaximumApplicationAddress);
    printf("--------------------------------------------------------------\n\n");

    std::vector<void*> bottomPtrs;
    std::vector<void*> topPtrs;

    // --- 实验组 A: 地板上的小蚂蚁 (普通分配) ---
    printf("[阶段 1] 模拟普通分配 (Bottom-Up) - 假装是 DLL 或小对象\n");
    for (int i = 0; i < 3; i++) {
        // 申请 1MB
        void* p = VirtualAlloc(NULL, 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p) {
            bottomPtrs.push_back(p);
            printf("  -> [地板分配 #%d] 地址: 0x%p (低地址)\n", i + 1, p);
        }
    }
    printf("  >> 观察结论: 地址以 0x0000... 开头，且在这个区域逐渐【变大】。\n\n");


    // --- 实验组 B: 天花板上的吊灯 (Top-Down) ---
    printf("[阶段 2] 模拟 UE 大内存池 (Top-Down) - 强行挂在最高处\n");
    for (int i = 0; i < 3; i++) {
        // 申请 100MB (模拟大内存块)
        // 关键标志位: MEM_TOP_DOWN
        void* p = VirtualAlloc(NULL, 100 * 1024 * 1024, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
        if (p) {
            topPtrs.push_back(p);
            printf("  -> [天花板分配 #%d] 地址: 0x%p (高地址)\n", i + 1, p);
        }
    }
    printf("  >> 观察结论: 地址以 0x7FF... 开头，且在这个区域逐渐【变小】(向下生长)。\n\n");


    // --- 实验组 C: 测量鸿沟 ---
    if (!bottomPtrs.empty() && !topPtrs.empty()) {
        unsigned long long lowAddr = (unsigned long long)bottomPtrs.back();
        unsigned long long highAddr = (unsigned long long)topPtrs.back();

        unsigned long long gap = highAddr - lowAddr;

        printf("==============================================================\n");
        printf("              最终结果分析                                    \n");
        printf("==============================================================\n");
        printf("低地址代表: 0x%p\n", (void*)lowAddr);
        printf("高地址代表: 0x%p\n", (void*)highAddr);
        printf("--------------------------------------------------------------\n");
        printf("中间未被污染的纯净空洞 (GAP) 大小: ");
        PrintFriendlySize(gap);
        printf("\n==============================================================\n");
    }

    // 清理内存
    for (void* p : bottomPtrs) VirtualFree(p, 0, MEM_RELEASE);
    for (void* p : topPtrs) VirtualFree(p, 0, MEM_RELEASE);

    getchar();
    return 0;
}