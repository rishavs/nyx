/* Checker rules for expressions: constructors, operators, match, if, and typeof. */

#include "compile_support.h"

TEST("range rejects zero literal step")
{
    return compile_fails("var r = range(0, 5, 0)\n", "zero range step",
                         MESSAGE_CHECKER_RANGE_ZERO_STEP);
}

TEST("range rejects converted zero step")
{
    return compile_fails("var r = range(0, 5, Int64(0))\n", "zero range step",
                         MESSAGE_CHECKER_RANGE_ZERO_STEP);
}

TEST("range rejects noninteger bounds")
{
    return compile_fails("var r = range(true, false)\n", "range bounds",
                         MESSAGE_CHECKER_RANGE_ARGUMENTS);
}

TEST("Range type requires an integer element")
{
    return compile_fails("var r: Range<Bool> = range(0, 1)\n", "range element",
                         MESSAGE_CHECKER_RANGE_ARGUMENTS);
}

TEST("range rejects mixed typed bounds")
{
    return compile_fails("var r = range(Int32(1), Int64(5))\n", "range bounds",
                         MESSAGE_CHECKER_RANGE_ARGUMENTS);
}

TEST("range rejects missing bounds")
{
    return compile_fails("var r = range(1)\n", "range arity",
                         MESSAGE_CHECKER_RANGE_ARGUMENTS);
}

TEST("Range cannot be redefined")
{
    return compile_fails("type Range value: Int64 end\n", "range type name",
                         MESSAGE_CHECKER_REDEFINES_BUILTIN, "Range");
}
TEST("field narrowing is invalidated by assignment")
{
    return compile_fails(
        "type Box value: Int32 | Nil end\n"
        "var box = Box(4)\n"
        "if typeof box.value is Int32 then\n"
        "  box.value = nil\n"
        "  print(box.value + 1)\n"
        "end\n", "field narrowing after mutation",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("array element narrowing is invalidated by assignment")
{
    return compile_fails(
        "var xs: Array<Int32 | Nil, 2> = [3, 4]\n"
        "if typeof xs[0] is Int32 then\n"
        "  xs[0] = nil\n"
        "  print(xs[0] + 1)\n"
        "end\n", "array narrowing after mutation",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("array element narrowing is invalidated when its index changes")
{
    return compile_fails(
        "var xs: Array<Int32 | Nil, 2> = [3, 4]\n"
        "var i: Int32 = 0\n"
        "if typeof xs[i] is Int32 then\n"
        "  i = 1\n"
        "  print(xs[i] + 1)\n"
        "end\n", "array narrowing after index changes",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("array narrowing tracks variables in computed indices")
{
    return compile_fails(
        "var xs: Array<Int32 | Nil, 2> = [3, 4]\n"
        "var i: Int32 = 1\n"
        "if typeof xs[i - 1] is Int32 then\n"
        "  i = 0\n"
        "  print(xs[i - 1] + 1)\n"
        "end\n", "array narrowing after computed index changes",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("list narrowing is invalidated through aliases")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32 | Nil> = arena.list<Int32 | Nil>(2)\n"
        "xs.push(Int32(3))\n"
        "var alias = xs\n"
        "if typeof xs[0] is Int32 then\n"
        "  alias[0] = nil\n"
        "  print(xs[0] + 1)\n"
        "end\n", "list narrowing after alias mutation",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("union subset narrowing is invalidated through aliases")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "fun use(x: Int32 | Bool) print(x) end\n"
        "var xs: List<Int32 | Bool | String<8>> = "
        "arena.list<Int32 | Bool | String<8>>(2)\n"
        "xs.push(Int32(3)) var alias = xs\n"
        "if !(typeof xs[0] is String<8>) then\n"
        "  alias[0] = \"new\"\n"
        "  use(xs[0])\n"
        "end\n", "subset narrowing after alias mutation",
        MESSAGE_CHECKER_FUNCTION_ARG_TYPE, (size_t)1, "use");
}

TEST("match list narrowing is invalidated through aliases")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var xs: List<Int32 | Nil> = arena.list<Int32 | Nil>(2)\n"
        "xs.push(Int32(3)) var alias = xs\n"
        "match typeof xs[0] with Int32 then\n"
        "  alias[0] = nil print(xs[0] + 1)\n"
        "with Nil then print(0) end\n",
        "match list narrowing after alias mutation",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("list narrowing is not read across an unsequenced call")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "fun mutate(xs: List<Int32 | Nil>): Int32 xs[0] = nil return 1 end\n"
        "var xs: List<Int32 | Nil> = arena.list<Int32 | Nil>(2)\n"
        "xs.push(Int32(3))\n"
        "if typeof xs[0] is Int32 then print(xs[0] + mutate(xs)) end\n",
        "list narrowing in a mutating expression",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("field narrowing is invalidated by method calls")
{
    return compile_fails(
        "type Box value: Int32 | Nil end\n"
        "method Box.clear() self.value = nil end\n"
        "var box = Box(4)\n"
        "if typeof box.value is Int32 then\n"
        "  box.clear()\n"
        "  print(box.value + 1)\n"
        "end\n", "field narrowing after method call",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("field narrowing is not read across an unsequenced mutation")
{
    return compile_fails(
        "type Box value: Int32 | Nil end\n"
        "method Box.clear(): Int32 self.value = nil return 1 end\n"
        "var box = Box(4)\n"
        "if typeof box.value is Int32 then\n"
        "  print(box.value + box.clear())\n"
        "end\n", "field narrowing in a mutating expression",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("a condition cannot preserve a field fact across a method call")
{
    return compile_fails(
        "type Box value: Int32 | Nil end\n"
        "method Box.clear(): Bool self.value = nil return true end\n"
        "var box = Box(4)\n"
        "if typeof box.value is Int32 && box.clear() then\n"
        "  print(box.value + 1)\n"
        "end\n", "field narrowing across condition call",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("constructor of non-struct")
{
    return compile_fails("var p = Bool(1)\n",
                             "constructing a non-struct type",
                             MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT, "Bool");
}

TEST("constructor argument count")
{
    return compile_fails("type P x: Int32 end\nvar p = P(1, 2)\n",
                             "a constructor with the wrong argument count",
                             MESSAGE_CHECKER_CONSTRUCTOR_ARG_COUNT,
                             "P", (size_t)1, (size_t)2);
}

TEST("missing constructor argument")
{
    return compile_fails("type P x: Int32 y: Int32 end\nvar p = P(x = 1)\n",
                             "a constructor missing a named argument",
                             MESSAGE_CHECKER_MISSING_CONSTRUCTOR_ARG, "y");
}

TEST("duplicate constructor argument")
{
    return compile_fails(
        "type P x: Int32 y: Int32 end\nvar p = P(x = 1, x = 2, y = 3)\n",
        "a constructor with a duplicate named argument",
        MESSAGE_CHECKER_DUPLICATE_CONSTRUCTOR_ARG, "x");
}

TEST("unknown constructor field")
{
    return compile_fails("type P x: Int32 end\nvar p = P(z = 1)\n",
                             "a constructor naming an unknown field",
                             MESSAGE_CHECKER_UNKNOWN_CONSTRUCTOR_FIELD, "z");
}

TEST("constructor argument type")
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(x:Int32):Int32\n  return x\nend\n"
        "var p = P(f)\n",
        "a constructor argument with the wrong type",
        MESSAGE_CHECKER_CONSTRUCTOR_ARG_TYPE, (size_t)1, "P");
}

TEST("type as value")
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(x:Int32):Int32\n  return P\nend\n",
        "a type used as a value",
        MESSAGE_CHECKER_TYPE_AS_VALUE, "P");
}

TEST("operator type")
{
    return compile_fails(
        "type P x:Int32 end\n"
        "fun f(p:P):P\n  return p + p\nend\n",
        "an operator applied to a non-Int32 value",
        MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("nil type mismatch")
{
    return compile_fails("var x: Int32 = nil\n",
                         "nil initialising an Int32",
                         MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "x",
                         "Int32");
}

TEST("Nil as a value")
{
    return compile_fails("var x = Nil\n", "the Nil type used as a value",
                         MESSAGE_CHECKER_TYPE_AS_VALUE, "Nil");
}

TEST("unknown field")
{
    return compile_fails(
        "type P\n  x: Int32\nend\nfun f(p: P): Int32\n  return p.y\nend\n",
        "unknown field",
        MESSAGE_CHECKER_UNKNOWN_FIELD, "y");
}

TEST("print as a value")
{
    return compile_fails(
        "var n: Int32 = print(1)\n",
        "print as a value",
        MESSAGE_CHECKER_PRINT_STATEMENT);
}

TEST("print named argument")
{
    return compile_fails(
        "print(x = 1)\n",
        "print named argument",
        MESSAGE_CHECKER_PRINT_NAMED);
}

TEST("if expression type mismatch")
{
    return compile_fails(
        "var z: Int32 = if true then 1 else false end\n",
        "if expression type mismatch",
        MESSAGE_CHECKER_IF_EXPR_MISMATCH);
}

TEST("match expression type mismatch")
{
    return compile_fails(
        "var b: Bool = true\n"
        "var z: Int32 = match b with true then 1 else false end\n",
        "match expression type mismatch",
        MESSAGE_CHECKER_IF_EXPR_MISMATCH);
}

TEST("non-exhaustive type match")
{
    return compile_fails(
        "type V = Int32 | Bool\n"
        "var x: V = 1\n"
        "match typeof x with Int32 then\n"
        "  print(\"i\")\n"
        "end\n",
        "non-exhaustive type match",
        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
}

TEST("non-exhaustive bool match")
{
    return compile_fails(
        "var x: Bool = true\n"
        "match x with true then\n"
        "  print(\"t\")\n"
        "end\n",
        "non-exhaustive bool match",
        MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
}

TEST("duplicate match pattern")
{
    return compile_fails(
        "type V = Int32 | Bool\n"
        "var x: V = 1\n"
        "match typeof x with Int32 then\n"
        "  print(\"i\")\n"
        "with Int32 then\n"
        "  print(\"j\")\n"
        "end\n",
        "duplicate match pattern",
        MESSAGE_CHECKER_MATCH_DUPLICATE);
}

TEST("match pattern does not fit")
{
    return compile_fails(
        "type V = Int32 | Bool\n"
        "var x: V = 1\n"
        "match typeof x with String<8> then\n"
        "  print(\"s\")\n"
        "else\n"
        "  print(\"o\")\n"
        "end\n",
        "match pattern does not fit",
        MESSAGE_CHECKER_MATCH_PATTERN);
}
