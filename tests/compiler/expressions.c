/* Checker rules for expressions: constructors, operators, match, if, and typeof. */

#include "compile_support.h"

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
        "var n: Nil = print(1)\n",
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
