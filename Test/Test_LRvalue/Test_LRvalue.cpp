#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

// ========== 黑魔法：提取真实类型名称 ==========
template <typename T>
constexpr std::string_view GetTypeName() {
#if defined(_MSC_VER)
    std::string_view sig = __FUNCSIG__;
    size_t start = sig.find("GetTypeName<") + 12;
    size_t end = sig.rfind(">(void)");
    return sig.substr(start, end - start);
#else
    std::string_view sig = __PRETTY_FUNCTION__;
    size_t start = sig.find("T = ") + 4;
    size_t end = sig.rfind("]");
    return sig.substr(start, end - start);
#endif
}

// ========== 核心：探测值类别 ==========
template <typename T>
constexpr std::string_view GetValueCategory() {
    if constexpr (std::is_lvalue_reference_v<T>) return "左值 (Lvalue)";
    else if constexpr (std::is_rvalue_reference_v<T>) return "将亡值 (Xvalue / 属于右值)";
    else return "纯右值 (Prvalue / 属于右值)";
}

// ========== 探测器 A：固定的重载网 ==========
void Catch(int& x) { std::cout << "被 [int&]       捕获\n"; }
void Catch(const int& x) { std::cout << "被 [const int&] 捕获\n"; }
void Catch(int&& x) { std::cout << "被 [int&&]      捕获\n"; }

// ========== 探测器 B：模板万能引用网 ==========
template <typename T>
void CatchT_Universal(T&& x) {
    std::cout << "被 [T&&] 捕获 -> 推导 T = " << GetTypeName<T>()
        << ", 最终参数 = " << GetTypeName<T&&>() << "\n";
}

// ========== 一键扒光的宏 ==========
#define ANALYZE(expr) \
    std::cout << "▶ 表达式: " << #expr << "\n" \
              << "  静态类型 (Type)   : " << GetTypeName<decltype(expr)>() << "\n" \
              << "  真实值类别(Category): " << GetValueCategory<decltype((expr))>() << "\n" \
              << "  普通函数捕获路由  : "; Catch(expr); \
    std::cout << "  模板 T&& 捕获路由 : "; CatchT_Universal(expr); \
    std::cout << "--------------------------------------------------\n"


// ==========================================
// 用于测试的全局变量和函数
// ==========================================
int g_val = 42;

int GetPrvalue() { return g_val; }             // 返回纯右值 (传值)
int& GetLvalueRef() { return g_val; }             // 返回左值引用
const int& GetConstLRef() { return g_val; }             // 返回 const 左值引用
int&& GetRvalueRef() { return std::move(g_val); }  // 返回右值引用


// ==========================================
// 核心测试引擎
// ==========================================
void RunAllLabs(int&& named_rvalue) {
    std::cout << "================ 场景 1: 基础变量与字面量 ================\n";
    int a = 10;
    const int b = 20;

    ANALYZE(a);
    ANALYZE(b);
    ANALYZE(100);

    std::cout << "\n================ 场景 2: 具名右值引用的“堕落” ================\n";
    // 它是右值引用类型，但它有名字，所以被当作左值处理
    ANALYZE(named_rvalue);

    std::cout << "\n================ 场景 3: std::move 强转 ================\n";
    ANALYZE(std::move(a));

    std::cout << "\n================ 场景 4: 函数返回纯右值 (传值) ================\n";
    ANALYZE(GetPrvalue());

    std::cout << "\n================ 场景 5: 函数返回左值引用 (int&) ================\n";
    ANALYZE(GetLvalueRef());

    std::cout << "\n================ 场景 6: 函数返回右值引用 (int&&) ================\n";
    ANALYZE(GetRvalueRef());

    std::cout << "\n================ 场景 7: 函数返回 const 左值引用 (const int&) ================\n";
    ANALYZE(GetConstLRef());
}

int main() {
    RunAllLabs(500);
    return 0;
}