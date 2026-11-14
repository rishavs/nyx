/* Numeric types and literals: ranges, radix and separators, conversions, and
   operators. */

#include "compile_support.h"

TEST("operator diagnostic names types")
{
    TestResult result = compile_fails("var x: Int32 = 1\nprint(x & true)\n",
                                      "binary operator operand types",
                                      MESSAGE_CHECKER_OPERATOR_TYPES,
                                      "&", "Int32", "Bool");
    if (result != TEST_PASS) return result;
    return compile_fails("print(~true)\n", "unary operator operand type",
                         MESSAGE_CHECKER_UNARY_OPERAND, "~", "Bool");
}

TEST("strict equality rejects unions")
{
    return compile_fails("var a: Int32 | Nil = 3\n"
                         "print(a === a)\n",
                         "strict union equality",
                         MESSAGE_CHECKER_OPERATOR_TYPE, "===");
}

TEST("default integer type and range")
{
    TestResult result = compile_accepts(
        "var x = 1\nvar y: Int64 = x\n"
        "var z: Uint64 = 18446744073709551615\n"
        "var min = -9223372036854775808\n"
        "var nested: Int32 = (1 + 2) * 3\n"
        "var small_float: Float32 = 1.5 + 2.5\n"
        "var wide: Uint64 = 18446744073709551615 - 1\n",
        "Int64 default and contextual Uint64");
    if (result != TEST_PASS) return result;
    result = compile_fails("var x = 9223372036854775808\n",
                           "inferred integer above Int64",
                           MESSAGE_CHECKER_INTEGER_RANGE);
    if (result != TEST_PASS) return result;
    result = compile_fails("print(9223372036854775808)\n",
                           "printed integer above Int64",
                           MESSAGE_CHECKER_INTEGER_RANGE);
    if (result != TEST_PASS) return result;
    return compile_fails("fun id<T>(x: T): T return x end\n"
                         "var x = id(9223372036854775808)\n",
                         "generic integer above Int64",
                         MESSAGE_CHECKER_INTEGER_RANGE);
}

TEST("byte: literal range")
{
    return compile_fails("var b: Byte = 300\n",
                         "a byte literal out of range",
                         MESSAGE_CHECKER_BYTE_RANGE, 300);
}

TEST("safety: Byte comparison range")
{
    return compile_accepts("var b: Byte = 1\nprint(300 == b)\n",
                           "numeric equality across integer ranges");
}

TEST("Int8 literal range")
{
    return compile_fails("var x: Int8 = 128\n", "Int8 literal range",
                         MESSAGE_CHECKER_INTEGER_RANGE);
}

TEST("Uint8 literal range")
{
    return compile_fails("var x: Uint8 = 256\n", "Uint8 literal range",
                         MESSAGE_CHECKER_BYTE_RANGE, 256);
}

TEST("Int64 maximum literal")
{
    return compile_accepts("var x: Int64 = 9223372036854775807\n",
                    "Int64 maximum literal");
}

TEST("Uint64 maximum literal")
{
    return compile_accepts("var x: Uint64 = 18446744073709551615\n",
                    "Uint64 maximum literal");
}

TEST("Uint64 literal overflow")
{
    return compile_fails("var x: Uint64 = 18446744073709551616\n",
                         "Uint64 literal overflow",
                         MESSAGE_LEXER_INTEGER_OUT_OF_RANGE);
}

TEST("Float32 literal")
{
    return compile_accepts("var x: Float32 = 1.5\n", "Float32 literal");
}

TEST("float literal overflow")
{
    return compile_fails("var x: Float64 = 1e999\n",
                         "float literal overflow",
                         MESSAGE_LEXER_FLOAT_OUT_OF_RANGE);
}

TEST("conversion argument count")
{
    return compile_fails("var x: Int32 = Int32()\n",
                         "conversion argument count",
                         MESSAGE_CHECKER_CONVERSION_ARGS);
}

TEST("conversion source type")
{
    return compile_fails("var x: Int32 = Int32(true)\n",
                         "conversion source type",
                         MESSAGE_CHECKER_CONVERSION_TYPE);
}

TEST("mixed operand types")
{
    return compile_fails(
        "var a: Int32 = 1\nvar b: Int64 = 2\nprint(a + b)\n",
        "mixed numeric types", MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("mixed numeric equality stays distinct from ordering")
{
    TestResult result = compile_accepts(
        "var a: Int32 = 3\nvar b: Uint64 = 3\n"
        "var f: Float64 = 3.0\n"
        "print(a == b, a != f, 3 == 3.0)\n",
        "mixed numeric equality");
    if (result != TEST_PASS) return result;
    result = compile_fails("var a: Int32 = 3\nvar b: Int64 = 3\n"
                           "print(a < b)\n",
                           "mixed numeric ordering",
                           MESSAGE_CHECKER_OPERATOR_TYPE, "<");
    if (result != TEST_PASS) return result;
    result = compile_fails("print(9223372036854775808 == 1)\n",
                           "out-of-range default integer in equality",
                           MESSAGE_CHECKER_INTEGER_RANGE);
    if (result != TEST_PASS) return result;
    return compile_fails("print(1 == true)\n", "numeric and Bool equality",
                         MESSAGE_CHECKER_OPERATOR_TYPE, "==");
}

TEST("Int8 shift width")
{
    return compile_fails("var a: Int8 = 1\nprint(a << 8)\n",
                         "Int8 shift width",
                         MESSAGE_CHECKER_SHIFT_RANGE);
}

TEST("float modulo")
{
    return compile_fails("print(1.5 % 2.0)\n", "float modulo",
                         MESSAGE_CHECKER_OPERATOR_TYPE, "%");
}

TEST("Float32 literal range")
{
    return compile_fails("var x: Float32 = 1e39\n", "Float32 literal range",
                         MESSAGE_CHECKER_FLOAT_RANGE);
}

TEST("union literal range")
{
    return compile_fails("var u: Int8 | Nil = 300\n",
                         "union literal range",
                         MESSAGE_CHECKER_INTEGER_RANGE);
}

TEST("union literal ambiguity")
{
    return compile_fails("var u: Int8 | Int32 = 5\n",
                         "ambiguous union literal",
                         MESSAGE_CHECKER_NUMERIC_AMBIGUOUS);
}

TEST("union literal unique alternative")
{
    return compile_accepts("var a: Int8 | Nil = -5\n"
                    "var b: Int64 | Nil = 5\n"
                    "var c: Float32 | Nil = 2.5\n"
                    "var d: Int32 | Int64 = 5\n",
                    "unique union literal alternatives");
}

TEST("huge literal in a Byte union")
{
    /* The literal does not fit an int, so it must not be read as Byte 0. */
    return compile_fails("var u: Byte | Nil = 4000000000\n",
                         "huge literal in a Byte union",
                         MESSAGE_CHECKER_INTEGER_RANGE);
}

TEST("constant Int8 conversion range")
{
    return compile_fails("print(Int8(300))\n", "constant Int8 conversion",
                         MESSAGE_CHECKER_CONVERSION_RANGE);
}

TEST("negative constant to unsigned")
{
    return compile_fails("print(Uint8(-1))\n",
                         "negative constant to unsigned",
                         MESSAGE_CHECKER_CONVERSION_RANGE);
}

TEST("constant Float32 conversion range")
{
    return compile_fails("print(Float32(1e39))\n",
                         "constant Float32 conversion",
                         MESSAGE_CHECKER_CONVERSION_RANGE);
}

TEST("nested constant conversion range")
{
    return compile_fails("print(Int8(Int32(200)))\n",
                         "nested constant conversion",
                         MESSAGE_CHECKER_CONVERSION_RANGE);
}

TEST("constant conversions fold")
{
    return compile_accepts("var a: Int8 = Int8(-128)\n"
                    "var b: Int32 = Int32(2.9)\n"
                    "var c: Uint64 = Uint64(18446744073709551615)\n"
                    "var d: Int64 = Int64(-9223372036854775808)\n"
                    "var e: Float32 = Float32(0.1)\n",
                    "constant conversions at their limits");
}

TEST("conversion keeps its type")
{
    /* An explicit conversion has a fixed type; context never retypes it. */
    return compile_fails("var x: Int64 = Int32(5)\n",
                         "conversion result keeps its type",
                         MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "x",
                         "Int64");
}

TEST("integer variable with float literal")
{
    return compile_fails("var a: Int32 = 1\nprint(a + 1.5)\n",
                         "integer variable beside a float literal",
                         MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("float variable with integer literal")
{
    return compile_fails("var a: Float64 = 1.5\nprint(a + 1)\n",
                         "float variable beside an integer literal",
                         MESSAGE_CHECKER_OPERATOR_TYPE, "+");
}

TEST("negated literal operand")
{
    return compile_accepts("var x: Int8 = 5\nvar y: Int64 = -5\n"
                    "var f: Float32 = 1.5\n"
                    "print(x + -1, x < -5, y == -5, -1 + y, f * -0.5)\n",
                    "negated literal beside a typed operand");
}

TEST("negated literal range")
{
    return compile_fails("var x: Int8 = 5\nprint(x < -129)\n",
                         "negated literal range",
                         MESSAGE_CHECKER_INTEGER_RANGE);
}

TEST("unsigned negation")
{
    return compile_fails("var a: Uint8 = 1\nprint(-a)\n",
                         "negating an unsigned value",
                         MESSAGE_CHECKER_OPERATOR_TYPE, "-");
}

TEST("bitwise on floats")
{
    return compile_fails("print(1.5 & 2.5)\n", "bitwise on floats",
                         MESSAGE_CHECKER_OPERATOR_TYPE, "&");
}

TEST("bitwise not on a float")
{
    return compile_fails("var f: Float64 = 1.5\nprint(~f)\n",
                         "bitwise not on a float",
                         MESSAGE_CHECKER_OPERATOR_TYPE, "~");
}

TEST("comparison needs one type")
{
    return compile_fails(
        "var a: Int32 = 1\nvar b: Uint32 = 1\nprint(a < b)\n",
        "comparing different integer types",
        MESSAGE_CHECKER_OPERATOR_TYPE, "<");
}

TEST("Uint64 shift width")
{
    return compile_fails("var a: Uint64 = 1\nprint(a << 64)\n",
                         "Uint64 shift width",
                         MESSAGE_CHECKER_SHIFT_RANGE);
}

TEST("Int64 shift width")
{
    return compile_accepts("var a: Int64 = 1\nprint(a << 63)\n",
                    "widest Int64 shift");
}

TEST("denormal float literals")
{
    return compile_accepts("var x: Float64 = 1e-310\nvar y: Float64 = 4.9e-324\n",
                    "denormal float literals");
}

TEST("float literal forms")
{
    return compile_accepts("var a: Float64 = 1e3\nvar b: Float64 = 1.5e-3\n"
                    "var c: Float64 = 2.5E+2\nvar d: Float64 = 0.25\n",
                    "float literal forms");
}

TEST("negative float literal overflow")
{
    return compile_fails("var x: Float64 = -1e999\n",
                         "negative float literal overflow",
                         MESSAGE_LEXER_FLOAT_OUT_OF_RANGE);
}

TEST("radix literals")
{
    return compile_accepts("var a: Int32 = 0xFF\n"
                    "var b: Int32 = 0b1010_1010\n"
                    "var c: Int32 = 0o755\n"
                    "var d: Uint64 = 0xFFFF_FFFF_FFFF_FFFF\n"
                    "var e: Int64 = 0x7FFF_FFFF_FFFF_FFFF\n"
                    "var f: Int8 = -0x80\n"
                    "var g: Uint8 = 0xff\n"
                    "var h: Array<Uint8, 0x10> = []\n"
                    "global arena: Store = Store(0x1000)\n",
                    "radix literals");
}

TEST("hex literal range")
{
    /* A radix literal gets the same range check as a decimal one. */
    return compile_fails("var x: Int8 = 0x80\n", "hex literal range",
                         MESSAGE_CHECKER_INTEGER_RANGE);
}

TEST("binary literal byte range")
{
    return compile_fails("var x: Uint8 = 0b1_0000_0000\n",
                         "binary literal range", MESSAGE_CHECKER_BYTE_RANGE,
                         256);
}

TEST("hex literal overflow")
{
    return compile_fails("var x: Uint64 = 0x1_0000_0000_0000_0000\n",
                         "hex literal overflow",
                         MESSAGE_LEXER_INTEGER_OUT_OF_RANGE);
}

TEST("untyped hex literal default")
{
    /* 0xFFFFFFFF does not fit Int32, so it defaults to Int64 like decimal. */
    return compile_accepts("var x = 0xFFFFFFFF\nvar y: Int64 = x\n",
                    "untyped hex literal defaults to Int64");
}

TEST("invalid binary digit")
{
    return compile_fails("print(0b102)\n", "invalid binary digit",
                         MESSAGE_LEXER_INVALID_DIGIT, '2', 2u);
}

TEST("invalid octal digit")
{
    return compile_fails("print(0o8)\n", "invalid octal digit",
                         MESSAGE_LEXER_INVALID_DIGIT, '8', 8u);
}

TEST("invalid hex digit")
{
    return compile_fails("print(0xFG)\n", "invalid hex digit",
                         MESSAGE_LEXER_INVALID_DIGIT, 'G', 16u);
}

TEST("fraction on a hex literal")
{
    return compile_fails("print(0x1.5)\n", "fraction on a hex literal",
                         MESSAGE_LEXER_INVALID_DIGIT, '.', 16u);
}

TEST("base prefix without digits")
{
    return compile_fails("print(0x)\n", "hex prefix without digits",
                         MESSAGE_LEXER_MISSING_DIGITS);
}

TEST("uppercase base prefix")
{
    return compile_fails("print(0XFF)\n", "uppercase hex prefix",
                         MESSAGE_LEXER_UPPERCASE_PREFIX, 'X');
}

TEST("separator after the prefix")
{
    return compile_fails("print(0x_FF)\n", "separator after the prefix",
                         MESSAGE_LEXER_BAD_SEPARATOR);
}

TEST("trailing separator")
{
    return compile_fails("print(1_)\n", "trailing separator",
                         MESSAGE_LEXER_BAD_SEPARATOR);
}

TEST("doubled separator")
{
    return compile_fails("print(1__000)\n", "doubled separator",
                         MESSAGE_LEXER_BAD_SEPARATOR);
}

TEST("separator beside a point")
{
    return compile_fails("print(1_.5)\n", "separator before a point",
                         MESSAGE_LEXER_BAD_SEPARATOR);
}

TEST("separators compile")
{
    return compile_accepts("var a: Int32 = 1_000_000\n"
                    "var b: Float64 = 1_000.000_1\n"
                    "var c: Float64 = 1e1_0\n",
                    "digit separators in decimals and floats");
}

TEST("built-in scalar names and numeric conversions")
{
    return compile_accepts(
        "var a: Int8 = Int8(1)\nvar b: Int16 = Int16(1)\n"
        "var c: Int32 = Int32(1)\nvar d: Int64 = Int64(1)\n"
        "var e: Uint8 = Uint8(1)\nvar f: Byte = Byte(1)\n"
        "var g: Uint16 = Uint16(1)\nvar h: Uint32 = Uint32(1)\n"
        "var i: Uint64 = Uint64(1)\n"
        "var j: Float32 = Float32(1)\nvar k: Float64 = Float64(1)\n"
        "var valid: Bool = true\n",
        "built-in scalar type names and numeric conversions");
}
