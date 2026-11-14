#include "compile_support.h"

TEST("unsupported nested equality")
{
    const char *bad[] = {
        "type Box value: Int32 | Nil end var a = Box(3) print(a == a)\n",
        "var a: Array<Int32 | Nil, 1> = [3] print(a == a)\n",
        "var a: Record<String<4>, Int32 | Nil, 1> = {\"a\": 3} print(a == a)\n",
        "global s: Store = Store(32) var a: Table<String<4>, Int32 | Nil> = s.table<String<4>, Int32 | Nil>(1) print(a == a)\n",
        "var a: Array<Int32, 1> = [1] var b: Array<Int64, 1> = [1] print(a == b)\n"
    };
    size_t i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TestResult result = compile_fails(bad[i], "unsupported equality",
                                          MESSAGE_CHECKER_OPERATOR_TYPE, "==");
        if (result != TEST_PASS) return result;
    }
    return TEST_PASS;
}

TEST("handle equality requires matching types")
{
    return compile_fails(
        "fun f(a: Handle<Int32>, b: Handle<Int64>): Bool return a == b end\n",
        "handles of different types", MESSAGE_CHECKER_OPERATOR_TYPE, "==");
}
