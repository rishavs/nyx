/* Checker rules for statements and control flow. */

#include "compile_support.h"

TEST("expression statement must be a call")
{
    return compile_fails("fun f(x:Int32):Int32 x end\n",
                         "a non-call expression statement",
                         MESSAGE_CHECKER_EXPRESSION_STATEMENT);
}

TEST("condition type")
{
    return compile_fails(
        "fun f(x:Int32):Int32\n"
        "  if x then\n    return 1\n  end\n"
        "  return 0\n"
        "end\n",
        "an Int32 if condition",
        MESSAGE_CHECKER_CONDITION_TYPE);
}

TEST("break outside loop")
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  break\n  return x\nend\n",
        "break outside a loop",
        MESSAGE_CHECKER_BREAK_OUTSIDE_LOOP);
}

TEST("continue outside loop")
{
    return compile_fails(
        "fun f(x:Int32):Int32\n  continue\n  return x\nend\n",
        "continue outside a loop",
        MESSAGE_CHECKER_CONTINUE_OUTSIDE_LOOP);
}

TEST("for non-iterable")
{
    return compile_fails(
        "for x in 1 do\nend\n",
        "for non-iterable",
        MESSAGE_CHECKER_FOR_ITERABLE);
}
