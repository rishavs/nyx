/* Checker rules for Text, String<N>, TextBuilder, and Error. */

#include "compile_support.h"

TEST("Text index diagnostic")
{
    return compile_fails("var s: Text = \"hi\"\nprint(s[0])\n",
                         "Text indexing", MESSAGE_CHECKER_TEXT_INDEX);
}

TEST("dynamic to fixed conversion")
{
    return compile_fails(
        "var s: Text = \"x\"\nvar f: String<8> = s\n",
        "a dynamic Text assigned to a fixed Text",
        MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "f", "String<8>");
}

TEST("a string literal larger than its capacity")
{
    return compile_fails(
        "var s: String<4> = \"hello\"\n",
        "a string literal larger than its capacity",
        MESSAGE_CHECKER_STRING_TOO_LONG, (size_t)5, 4);
}

TEST("a string literal without a String<N> context")
{
    return compile_fails(
        "var s = \"x\"\n",
        "a string literal without a String<N> context",
        MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
}

TEST("comparing two uncontextual string literals")
{
    return compile_accepts(
        "var ok: Bool = \"a\" == \"b\"\n",
        "comparing two uncontextual string literals");
}

TEST("a literal that fits two union alternatives")
{
    return compile_fails(
        "var u: String<4> | String<8> = \"hi\"\n",
        "a literal that fits two union alternatives",
        MESSAGE_CHECKER_STRING_AMBIGUOUS);
}

TEST("a Text capacity above the limit")
{
    return compile_fails(
        "var s: String<4097> = \"\"\n",
        "a Text capacity above the limit",
        MESSAGE_CHECKER_STRING_CAPACITY, 4096);
}

TEST("assigning a String<8> to a String<16>")
{
    return compile_fails(
        "var a: String<8> = \"hi\"\nvar b: String<16> = a\n",
        "assigning a String<8> to a String<16>",
        MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "b", "String<16>");
}

TEST("comparing different Text capacities")
{
    return compile_accepts(
        "var a: String<8> = \"hi\"\nvar b: String<16> = \"hi\"\nvar ok: Bool "
        "= a == b\n",
        "comparing different Text capacities");
}

TEST("byte_len with an argument")
{
    return compile_fails(
        "var s: String<8> = \"hi\"\nvar n: Int32 = s.byte_len(1)\n",
        "byte_len with an argument",
        MESSAGE_CHECKER_MEMBER_ARGS, "byte_len");
}

TEST("byte_at with a string index")
{
    return compile_fails(
        "var s: String<8> = \"hi\"\nvar b: Byte = s.byte_at(\"x\")\n",
        "byte_at with a string index",
        MESSAGE_CHECKER_STRING_INDEX);
}

TEST("a type argument on byte_len")
{
    return compile_fails(
        "var s: String<8> = \"hi\"\nvar n: Int32 = s.byte_len<String<8>>()\n",
        "a type argument on byte_len",
        MESSAGE_CHECKER_MEMBER_TYPE_ARG, "byte_len");
}

TEST("byte_len on an Int32")
{
    return compile_fails(
        "var n: Int32 = 1.byte_len()\n",
        "byte_len on an Int32",
        MESSAGE_CHECKER_MEMBER_RECEIVER, "byte_len");
}

TEST("byte_len on an uncontextual literal")
{
    return compile_fails(
        "var n: Int32 = \"hi\".byte_len()\n",
        "byte_len on an uncontextual literal",
        MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
}

TEST("Error with too few arguments")
{
    return compile_fails(
        "var e: Error = Error(\"E\", \"d\")\n",
        "Error with too few arguments",
        MESSAGE_CHECKER_ERROR_ARGS);
}

TEST("Error with a non-string description")
{
    return compile_fails(
        "var e: Error = Error(\"E\", 1, \"f\", 1, 2, 3)\n",
        "Error with a non-string description",
        MESSAGE_CHECKER_ERROR_ARG_TYPE, (size_t)2);
}

TEST("Error with named arguments")
{
    return compile_fails(
        "var e: Error = Error(header=\"E\", description=\"d\", "
        "filename=\"f\", line=1, col=2, pos=3)\n",
        "Error with named arguments",
        MESSAGE_CHECKER_ERROR_ARGS);
}

TEST("an Error field method on an Int32")
{
    return compile_fails(
        "var n: Int32 = 1.line()\n",
        "an Error field method on an Int32",
        MESSAGE_CHECKER_MEMBER_RECEIVER, "line");
}

TEST("concat with a fixed string")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var s: Text = arena.concat(f, \"y\")\n",
        "concat with a fixed string",
        MESSAGE_CHECKER_CONCAT_TYPE);
}

TEST("copy with a fixed string")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var s: Text = arena.copy(f)\n",
        "copy with a fixed string",
        MESSAGE_CHECKER_COPY_TYPE);
}

TEST("store.text with a dynamic literal")
{
    return compile_fails(
        "global arena: Store = Store(16)\nvar s: Text = arena.text(\"hi\")\n",
        "store.text with a dynamic literal",
        MESSAGE_CHECKER_STORE_TEXT_TYPE);
}

TEST("slice with a non-Int32 start")
{
    return compile_fails(
        "var s: Text = \"hello\"\nvar t: Text = s.slice(\"x\", 1)\n",
        "slice with a non-Int32 start",
        MESSAGE_CHECKER_STRING_SLICE_TYPE);
}

TEST("comparing fixed and dynamic strings")
{
    return compile_accepts(
        "var f: String<8> = \"x\"\nvar s: Text = \"x\"\nvar ok: Bool = f == s\n",
        "comparing fixed and dynamic strings");
}

TEST("builder: builder with a non-Int32 capacity")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var b: TextBuilder = arena.builder(\"x\")\n",
        "builder with a non-Int32 capacity",
        MESSAGE_CHECKER_BUILDER_CAPACITY);
}

TEST("builder: builder append with a fixed string")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var b: TextBuilder = arena.builder(4)\n"
        "b.append(f)\n",
        "builder append with a fixed string",
        MESSAGE_CHECKER_BUILDER_APPEND_TYPE);
}

TEST("builder: builder append_byte with a string")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var b: TextBuilder = arena.builder(4)\n"
        "b.append_byte(\"x\")\n",
        "builder append_byte with a string",
        MESSAGE_CHECKER_BUILDER_BYTE_TYPE);
}

TEST("builder: builder finish with an argument")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var b: TextBuilder = arena.builder(4)\n"
        "var s: Text = b.finish(1)\n",
        "builder finish with an argument",
        MESSAGE_CHECKER_MEMBER_ARGS, "finish");
}

TEST("Error field write")
{
    return compile_fails(
        "fun f(e: Error)\n  e.line = 1\n  return\nend\n",
        "Error field write",
        MESSAGE_CHECKER_FIELD_LVALUE);
}

TEST("bare String")
{
    return compile_fails(
        "var s: String = \"hello\"\n",
        "bare String",
        MESSAGE_CHECKER_STRING_ARGS);
}

TEST("Error::throw argument count")
{
    return compile_fails(
        "var e: Error = Error::throw(\"E\")\n",
        "Error::throw argument count",
        MESSAGE_CHECKER_ERROR_THROW_ARGS);
}

TEST("Error.throw requires static syntax")
{
    return compile_fails(
        "var e: Error = Error.throw(\"E\", \"description\")\n",
        "the old Error.throw syntax",
        MESSAGE_CHECKER_UNKNOWN_MEMBER, "throw");
}
