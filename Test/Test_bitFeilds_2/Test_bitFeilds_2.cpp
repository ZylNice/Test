#include <iostream>
#include <cstdint>
#include <bitset>
#include <string>

// 模拟 UE 源码中的 FPoolInfoSmall 头部
struct FPoolInfoSmall_Header {
    uint32_t Canary : 2;  // 预期占据：Bit 0 ~ 1
    uint32_t Taken : 15; // 预期占据：Bit 2 ~ 16
    uint32_t NoFirstFreeIndex : 1;  // 预期占据：Bit 17
    uint32_t FirstFreeIndex : 14; // 预期占据：Bit 18 ~ 31
};

// 使用 union 强行透视内存
union MemoryInspector {
    FPoolInfoSmall_Header header;
    uint32_t raw_memory; // 刚好 1 个 32位整数，共 4 字节
};

int main() {
    MemoryInspector inspector;

    std::cout << "=== FPoolInfoSmall_Header 内存排布透视 ===\n";
    std::cout << "总大小: " << sizeof(FPoolInfoSmall_Header) << " 字节\n\n";

    // ---------------------------------------------------------
    // 测试：分别点亮每个变量，观察它们在 32 位内存中的地盘
    // ---------------------------------------------------------

    // 1. 点亮 Canary (2位)
    inspector.raw_memory = 0;
    inspector.header.Canary = 0x3; // 二进制 11
    std::string b1 = std::bitset<32>(inspector.raw_memory).to_string();
    std::cout << "1. Canary占据 (2位):\n";
    std::cout << b1.substr(0, 14) << " " << b1.substr(14, 1) << " " << b1.substr(15, 15) << " " << b1.substr(30, 2) << "\n\n";

    // 2. 点亮 Taken (15位)
    inspector.raw_memory = 0;
    inspector.header.Taken = 0x7FFF; // 15个 1
    std::string b2 = std::bitset<32>(inspector.raw_memory).to_string();
    std::cout << "2. Taken占据 (15位):\n";
    std::cout << b2.substr(0, 14) << " " << b2.substr(14, 1) << " " << b2.substr(15, 15) << " " << b2.substr(30, 2) << "\n\n";

    // 3. 点亮 NoFirstFreeIndex (1位)
    inspector.raw_memory = 0;
    inspector.header.NoFirstFreeIndex = 0x1; // 1个 1
    std::string b3 = std::bitset<32>(inspector.raw_memory).to_string();
    std::cout << "3. NoFirstFreeIndex占据 (1位):\n";
    std::cout << b3.substr(0, 14) << " " << b3.substr(14, 1) << " " << b3.substr(15, 15) << " " << b3.substr(30, 2) << "\n\n";

    // 4. 点亮 FirstFreeIndex (14位)
    inspector.raw_memory = 0;
    inspector.header.FirstFreeIndex = 0x3FFF; // 14个 1
    std::string b4 = std::bitset<32>(inspector.raw_memory).to_string();
    std::cout << "4. FirstFreeIndex占据 (14位):\n";
    std::cout << b4.substr(0, 14) << " " << b4.substr(14, 1) << " " << b4.substr(15, 15) << " " << b4.substr(30, 2) << "\n\n";

    return 0;
}