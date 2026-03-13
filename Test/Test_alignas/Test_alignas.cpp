#include <iostream>
#include <cstddef>

// 1. 普通情况：什么都不加
struct S1_Normal {
    int a;
    int b;
};

// 2. 陷阱情况：只给第一个变量加 alignas
struct S2_AlignFirst {
    alignas(64) int a;
    int b;
};

// 3. 错位情况：只给第二个变量加 alignas
struct S3_AlignSecond {
    int a;
    alignas(64) int b;
};

// 4. UE 正确写法：两个变量都加 alignas
struct S4_AlignBoth {
    alignas(64) int a;
    alignas(64) int b;
};

// 5. 官方例子写法：修饰整个结构体
struct alignas(64) S5_AlignStruct {
    int a;
    int b;
};

// 6. 混合类型情况：不同大小的类型穿插对齐
struct S6_Mixed {
    char a;                 // 1 字节
    alignas(32) double b;   // 8 字节，要求 32 字节对齐
    int c;                  // 4 字节
};

// 辅助打印宏
#define PRINT_INFO(Type, obj) \
    std::cout << "--- " << #Type << " ---\n"; \
    std::cout << "总大小 (sizeof): " << sizeof(Type) << " 字节\n"; \
    std::cout << "&a 真实地址: " << (void*)&obj.a << " | a 偏移: " << offsetof(Type, a) << "\n"; \
    std::cout << "&b 真实地址: " << (void*)&obj.b << " | b 偏移: " << offsetof(Type, b) << "\n\n";

int main() {
    std::cout << std::hex << std::showbase; // 显示真实十六进制地址

    S1_Normal s1;       PRINT_INFO(S1_Normal, s1);
    S2_AlignFirst s2;   PRINT_INFO(S2_AlignFirst, s2);
    S3_AlignSecond s3;  PRINT_INFO(S3_AlignSecond, s3);
    S4_AlignBoth s4;    PRINT_INFO(S4_AlignBoth, s4);
    S5_AlignStruct s5;  PRINT_INFO(S5_AlignStruct, s5);

    std::cout << "--- S6_Mixed ---\n";
    std::cout << "总大小: " << std::dec << sizeof(S6_Mixed) << " 字节\n";
    std::cout << "a 偏移: " << offsetof(S6_Mixed, a) << "\n";
    std::cout << "b 偏移: " << offsetof(S6_Mixed, b) << "\n";
    std::cout << "c 偏移: " << offsetof(S6_Mixed, c) << "\n";

    return 0;
}