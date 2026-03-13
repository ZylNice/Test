#include <iostream>
#include <cstdint>

// 模拟 CPU 缓存行大小
constexpr size_t CACHE_LINE_SIZE = 64;

// ==========================================
// 定义四种不同对齐策略的结构体
// 假设在 64 位系统上，void* 是 8 字节，8 个指针正好是 64 字节。
// ==========================================

// 情况 1：普通结构体（无特殊对齐，默认跟随最大成员 void* 的 8 字节对齐）
struct NormalStruct {
    void* ptrs[8];
};

// 情况 2：仅结构体对齐（你的推论：内部声明了对齐，外部变量应该自动对齐）
struct alignas(CACHE_LINE_SIZE) AlignedStruct {
    void* ptrs[8];
};

// 情况 3：仅结构体对齐，但故意少放点数据，看 Padding（填充）效果
struct alignas(CACHE_LINE_SIZE) PaddingStruct {
    void* ptrs[1]; // 实际只有 8 字节数据
};

// 情况 4：虚幻引擎的双重对齐结构体
struct alignas(CACHE_LINE_SIZE) UeStyleStruct {
    void* ptrs[8];
};

// ==========================================
// 全局变量声明测试
// ==========================================
NormalStruct                        g_NormalArray[2];
AlignedStruct                       g_AlignedArray[2];        // 仅结构体自带对齐
alignas(CACHE_LINE_SIZE) NormalStruct g_ArrayAlignedOnly[2];  // 仅在数组声明时强制对齐
alignas(CACHE_LINE_SIZE) UeStyleStruct g_UeStyleArray[2];     // UE 风格：双重对齐

// 辅助打印函数
void PrintMemoryInfo(const char* name, size_t size, size_t align, uintptr_t addr, uintptr_t addr2) {
    std::cout << "--- " << name << " ---\n";
    std::cout << "结构体大小 (sizeof)   : " << size << " bytes\n";
    std::cout << "结构体对齐 (alignof)  : " << align << " bytes\n";
    std::cout << "数组首地址 (Index 0)  : 0x" << std::hex << addr << std::dec
        << " (" << (addr % CACHE_LINE_SIZE == 0 ? "已 64 字节对齐 ✅" : "未 64 字节对齐 ❌") << ")\n";
    std::cout << "数组次地址 (Index 1)  : 0x" << std::hex << addr2 << std::dec << "\n";
    std::cout << "两元素间距 (步长)     : " << (addr2 - addr) << " bytes\n\n";
}

int main() {
    std::cout << "========== 全局变量测试 ==========\n";
    PrintMemoryInfo("1. 普通数组 (无对齐)",
        sizeof(NormalStruct), alignof(NormalStruct),
        reinterpret_cast<uintptr_t>(&g_NormalArray[0]), reinterpret_cast<uintptr_t>(&g_NormalArray[1]));

    PrintMemoryInfo("2. 仅结构体对齐 (你的推论)",
        sizeof(AlignedStruct), alignof(AlignedStruct),
        reinterpret_cast<uintptr_t>(&g_AlignedArray[0]), reinterpret_cast<uintptr_t>(&g_AlignedArray[1]));

    PrintMemoryInfo("3. 仅数组声明对齐",
        sizeof(NormalStruct), alignof(NormalStruct),
        reinterpret_cast<uintptr_t>(&g_ArrayAlignedOnly[0]), reinterpret_cast<uintptr_t>(&g_ArrayAlignedOnly[1]));

    PrintMemoryInfo("4. 双重对齐 (UE风格)",
        sizeof(UeStyleStruct), alignof(UeStyleStruct),
        reinterpret_cast<uintptr_t>(&g_UeStyleArray[0]), reinterpret_cast<uintptr_t>(&g_UeStyleArray[1]));


    std::cout << "========== 局部变量 (栈内存) 测试 ==========\n";
    NormalStruct                        l_NormalArray[2];
    AlignedStruct                       l_AlignedArray[2];
    alignas(CACHE_LINE_SIZE) NormalStruct l_ArrayAlignedOnly[2];
    alignas(CACHE_LINE_SIZE) UeStyleStruct l_UeStyleArray[2];

    PrintMemoryInfo("1. 普通数组 (栈区)",
        sizeof(NormalStruct), alignof(NormalStruct),
        reinterpret_cast<uintptr_t>(&l_NormalArray[0]), reinterpret_cast<uintptr_t>(&l_NormalArray[1]));

    PrintMemoryInfo("2. 仅结构体对齐 (栈区)",
        sizeof(AlignedStruct), alignof(AlignedStruct),
        reinterpret_cast<uintptr_t>(&l_AlignedArray[0]), reinterpret_cast<uintptr_t>(&l_AlignedArray[1]));

    PrintMemoryInfo("3. 仅数组声明对齐 (栈区)",
        sizeof(NormalStruct), alignof(NormalStruct),
        reinterpret_cast<uintptr_t>(&l_ArrayAlignedOnly[0]), reinterpret_cast<uintptr_t>(&l_ArrayAlignedOnly[1]));

    std::cout << "========== 结构体 Padding 测试 ==========\n";
    PrintMemoryInfo("5. 内部仅 8 字节数据的对齐结构体",
        sizeof(PaddingStruct), alignof(PaddingStruct),
        0, 0); // 仅看大小

    return 0;
}