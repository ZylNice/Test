#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

// ==========================================
// 黑魔法：提取真实类型名称 & 值类别探测
// ==========================================
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
std::forward();

template <typename T>
constexpr std::string_view GetValueCategory() {
    if constexpr (std::is_lvalue_reference_v<T>) return "左值 (Lvalue)";
    else if constexpr (std::is_rvalue_reference_v<T>) return "将亡值 (Xvalue / 属于右值)";
    else return "纯右值 (Prvalue / 属于右值)";
}

// ==========================================
// 探测器类：带内存地址的吵闹鬼
// ==========================================
class Tracker {
public:
    Tracker() {
        std::cout << "    [+] 默认构造实体 -> 内存开辟在: " << this << "\n";
    }
    Tracker(const Tracker& other) {
        std::cout << "    [C] 拷贝构造实体 -> 内存开辟在: " << this << " (源: " << &other << ")\n";
    }
    Tracker(Tracker&& other) noexcept {
        std::cout << "    [M] 移动构造实体 -> 内存开辟在: " << this << " (窃取自: " << &other << ")\n";
    }
    // 【新增】拷贝赋值运算符
    Tracker& operator=(const Tracker& other) {
        std::cout << "    [C=] 拷贝赋值实体 -> 目标: " << this << " (源: " << &other << ")\n";
        return *this;
    }
    // 【新增】移动赋值运算符
    Tracker& operator=(Tracker&& other) noexcept {
        std::cout << "    [M=] 移动赋值实体 -> 目标: " << this << " (窃取自: " << &other << ")\n";
        return *this;
    }
    ~Tracker() {
        std::cout << "    [-] 析构销毁实体 -> 回收内存于: " << this << "\n";
    }
};

// ==========================================
// 探测器网：捕获并打印实参的内存地址
// ==========================================
void Catch(Tracker& x) { std::cout << "  -> 被 [Tracker&] 捕获，引用的目标地址: " << &x << "\n"; }
void Catch(const Tracker& x) { std::cout << "  -> 被 [const Tracker&] 捕获，引用的目标地址: " << &x << "\n"; }
void Catch(Tracker&& x) { std::cout << "  -> 被 [Tracker&&] 捕获，引用的目标地址: " << &x << "\n"; }

template <typename T>
void CatchT_Universal(T&& x) {
    std::cout << "  -> 被 [T&&] 万能引用捕获 -> 推导 T = " << GetTypeName<T>()
        << ", 最终参数 = " << GetTypeName<T&&>()
        << ", 引用的目标地址: " << &x << "\n";
}

// ==========================================
// 制造工厂
// ==========================================
Tracker MakePrvalue() {
    std::cout << "  [工厂内部] 准备 return Tracker{}...\n";
    return Tracker{};
}

Tracker&& MakeXvalue(Tracker& t) {
    std::cout << "  [工厂内部] 准备 return std::move(t)...\n";
    return std::move(t);
}

Tracker MakeNRVO() {
    std::cout << "  [工厂内部] 创建局部变量 obj，准备直接 return obj (期待 NRVO)...\n";
    Tracker obj;
    return obj;
}

Tracker MakeImplicitMove(bool flag) {
    std::cout << "  [工厂内部] 创建局部变量 a 和 b，准备通过条件分支返回 (破坏 NRVO)...\n";
    Tracker a;
    Tracker b;
    if (flag) {
        std::cout << "  [工厂内部] 决定返回 a，期待触发【隐式移动】兜底...\n";
        return a;
    }
    else {
        return b;
    }
}

Tracker MakePessimizingMove() {
    std::cout << "  [工厂内部] 创建局部变量 obj，准备 return std::move(obj) (画蛇添足)...\n";
    Tracker obj;
    return std::move(obj);
}


// ==========================================
// 终极分析宏
// ==========================================
#define ANALYZE_COMPILE_TIME(expr) \
    std::cout << "▶ 表达式: " << #expr << "\n" \
              << "  [编译期诊断] 静态类型   : " << GetTypeName<decltype(expr)>() << "\n" \
              << "  [编译期诊断] 真实值类别 : " << GetValueCategory<decltype((expr))>() << "\n"

// ==========================================
// 核心测试引擎
// ==========================================
int main() {
    std::cout << "================ 场景 1: 验证【纯右值】的延迟构造实质 ================\n";
    ANALYZE_COMPILE_TIME(MakePrvalue());
    std::cout << "  [运行期捕获] 开始执行 Catch(MakePrvalue())...\n";
    Catch(MakePrvalue());
    std::cout << "\n";

    std::cout << "================ 场景 2: 验证【将亡值】不会触发新构造 ================\n";
    {
        Tracker real_obj;
        std::cout << "  [主函数] real_obj 活体已就绪，地址: " << &real_obj << "\n";
        ANALYZE_COMPILE_TIME(MakeXvalue(real_obj));
        std::cout << "  [运行期捕获] 开始执行 Catch(MakeXvalue(real_obj))...\n";
        Catch(MakeXvalue(real_obj));
    }
    std::cout << "\n";

    std::cout << "================ 场景 3: 验证神级优化【NRVO】(返回局部变量) ================\n";
    std::cout << "预期: 只发生 1 次构造，直接在外部 main 栈上创建，0 拷贝，0 移动！\n";
    {
        Tracker res = MakeNRVO(); // 注意：这是初始化！
        std::cout << "  [主函数] 接收完毕！res 的最终地址: " << &res << "\n";
    }
    std::cout << "\n";

    std::cout << "================ 场景 4: 验证【隐式移动】兜底 (NRVO 失败) ================\n";
    std::cout << "预期: 构造 2 个局部对象，然后自动把返回的那个当成右值【隐式移动】出来！\n";
    {
        Tracker res = MakeImplicitMove(true);
        std::cout << "  [主函数] 接收完毕！res 的最终地址: " << &res << "\n";
    }
    std::cout << "\n";

    std::cout << "================ 场景 5: 验证画蛇添足的【负优化】(return std::move) ================\n";
    std::cout << "预期: NRVO 特权被取消！强制在内部造对象，再【移动】出来，平白增加开销！\n";
    {
        Tracker res = MakePessimizingMove();
        std::cout << "  [主函数] 接收完毕！res 的最终地址: " << &res << "\n";
    }
    std::cout << "\n";

    // ---------- 全新加入的杀手锏场景 ----------
    std::cout << "================ 场景 6: 验证【提前构造】破功 (赋值而非初始化) ================\n";
    std::cout << "预期: 外部坑位已被占用，无法 NRVO！必须借助临时对象倒手，触发【移动赋值】！\n";
    {
        Tracker res; // 提前构造 (占据了物理内存)
        std::cout << "  [主函数] res 已经提前出生，占据了坑位: " << &res << "\n";
        std::cout << "  [主函数] 准备执行 res = MakeNRVO(); ...\n";

        res = MakeNRVO(); // 赋值操作！

        std::cout << "  [主函数] 赋值完毕！res 的最终地址依旧是: " << &res << "\n";
    }
    std::cout << "\n";

    return 0;
}