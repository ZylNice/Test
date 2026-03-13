#include <windows.h>
#include <iostream>
#include <iomanip>

// 模拟虚幻引擎的 FPlatformMath::RoundUpToPowerOfTwo64
// 作用：找到大于等于 v 的最小的 2 的幂次方
unsigned long long RoundUpToPowerOfTwo64(unsigned long long v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

// 辅助函数：把字节转换成容易阅读的 GB/MB
void PrintBytes(const char* Name, unsigned long long Bytes) {
    double GB = (double)Bytes / (1024.0 * 1024.0 * 1024.0);
    std::cout << std::left << std::setw(30) << Name
        << "= " << Bytes << " Bytes"
        << " (" << std::fixed << std::setprecision(2) << GB << " GB)\n";
}

int main() {
    std::cout << "=== Windows API Raw Data ===\n";

    // 1. 获取内存状态 (对应 GlobalMemoryStatusEx)
    MEMORYSTATUSEX MemoryStatusEx;
    ZeroMemory(&MemoryStatusEx, sizeof(MemoryStatusEx));
    MemoryStatusEx.dwLength = sizeof(MemoryStatusEx);
    GlobalMemoryStatusEx(&MemoryStatusEx);

    // 2. 获取系统信息 (对应 GetSystemInfo)
    SYSTEM_INFO SystemInfo;
    ZeroMemory(&SystemInfo, sizeof(SystemInfo));
    GetSystemInfo(&SystemInfo);

    std::cout << "\n=== 模拟 UE FPlatformMemoryConstants 赋值 ===\n";

    // 物理内存总数
    unsigned long long TotalPhysical = MemoryStatusEx.ullTotalPhys;
    PrintBytes("TotalPhysical", TotalPhysical);

    // 虚拟内存总数 (UE 故意用了 ullTotalPageFile，代表 物理内存 + 虚拟内存页面文件)
    unsigned long long TotalVirtual = MemoryStatusEx.ullTotalPageFile;
    PrintBytes("TotalVirtual", TotalVirtual);

    // 系统分配粒度 (通常是 64KB)
    DWORD BinnedPageSize = SystemInfo.dwAllocationGranularity;
    std::cout << std::left << std::setw(30) << "BinnedPageSize"
        << "= " << BinnedPageSize << " Bytes (" << BinnedPageSize / 1024 << " KB)\n";

    // 物理页面大小 (通常是 4KB)
    DWORD BinnedAllocationGranularity = SystemInfo.dwPageSize;
    std::cout << std::left << std::setw(30) << "BinnedAllocGranularity"
        << "= " << BinnedAllocationGranularity << " Bytes (" << BinnedAllocationGranularity / 1024 << " KB)\n";

    // OS 分配粒度 (等同于 BinnedPageSize，64KB)
    DWORD OsAllocationGranularity = SystemInfo.dwAllocationGranularity;
    std::cout << std::left << std::setw(30) << "OsAllocationGranularity"
        << "= " << OsAllocationGranularity << " Bytes (" << OsAllocationGranularity / 1024 << " KB)\n";

    // 基础页面大小 (等同于 BinnedAllocGranularity，4KB)
    DWORD PageSize = SystemInfo.dwPageSize;
    std::cout << std::left << std::setw(30) << "PageSize"
        << "= " << PageSize << " Bytes (" << PageSize / 1024 << " KB)\n";

    // 寻址极限 (将物理内存向上取整到最近的 2 的幂)
    unsigned long long AddressLimit = RoundUpToPowerOfTwo64(TotalPhysical);
    PrintBytes("AddressLimit", AddressLimit);

    std::cout << "\n=============================================\n";
    system("pause");
    return 0;
}