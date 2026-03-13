#include <windows.h>
#include <stdio.h>

int main() {
    // 1. 获取系统信息，看看 Windows 自己声称粒度是多少
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    printf("System Allocation Granularity: %u bytes (Should be 65536)\n\n", si.dwAllocationGranularity);

    // 2. 第一次申请：只申请 1 个字节 (极小)
    // 使用 VirtualAlloc，它是最底层的 API
    void* p1 = VirtualAlloc(NULL, 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    // 3. 第二次申请：再申请 1 个字节
    void* p2 = VirtualAlloc(NULL, 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    // 4. 打印地址
    printf("Pointer 1 Address: %p\n", p1);
    printf("Pointer 2 Address: %p\n", p2);

    // 5. 计算差距
    long long diff = (char*)p2 - (char*)p1;
    printf("\nDifference: %lld bytes\n", diff);

    if (diff == 65536) {
        printf("CONCLUSION: Proven! Even 1 byte consumed a 64KB slot.\n");
    }
    else {
        printf("CONCLUSION: Disproven.\n");
    }

    return 0;
}