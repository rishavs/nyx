/* base64("..."): decoding, every malformed form it rejects, and the reserved name. */

#include "compile_support.h"

TEST("literals compile")
{
    return compile_accepts("var a: Text = base64(\"SGVsbG8=\")\n"
                    "var b: Text = base64(\"\")\n"
                    "var c: Text | Nil = base64(\"AAECgP8=\")\n"
                    "var d = base64(\"+/+/\")\n",
                    "base64 literals");
}

TEST("needs a literal")
{
    return compile_fails("var s: Text = \"SGk=\"\nvar t: Text = base64(s)\n",
                         "base64 of a variable",
                         MESSAGE_CHECKER_BASE64_ARGUMENT);
}

TEST("argument count")
{
    return compile_fails("var t: Text = base64()\n",
                         "base64 without an argument",
                         MESSAGE_CHECKER_BASE64_ARGUMENT);
}

TEST("invalid character")
{
    return compile_fails("var t: Text = base64(\"SG!=\")\n",
                         "base64 invalid character",
                         MESSAGE_CHECKER_BASE64_CHARACTER, (size_t)2);
}

TEST("whitespace")
{
    return compile_fails("var t: Text = base64(\"SG k\")\n",
                         "base64 whitespace",
                         MESSAGE_CHECKER_BASE64_CHARACTER, (size_t)2);
}

TEST("URL-safe alphabet")
{
    /* Only the standard alphabet: '-' and '_' are the URL-safe spellings. */
    return compile_fails("var t: Text = base64(\"SGk-\")\n",
                         "base64 URL-safe alphabet",
                         MESSAGE_CHECKER_BASE64_CHARACTER, (size_t)3);
}

TEST("bad length")
{
    return compile_fails("var t: Text = base64(\"SGk\")\n",
                         "base64 unpadded length",
                         MESSAGE_CHECKER_BASE64_PADDING, (size_t)3);
}

TEST("misplaced padding")
{
    return compile_fails("var t: Text = base64(\"SG=k\")\n",
                         "base64 padding before data",
                         MESSAGE_CHECKER_BASE64_PADDING, (size_t)3);
}

TEST("padding in a middle group")
{
    return compile_fails("var t: Text = base64(\"SGk=SGk=\")\n",
                         "base64 padding in a middle group",
                         MESSAGE_CHECKER_BASE64_PADDING, (size_t)3);
}

TEST("non-canonical padding bits")
{
    /* "QR==" would decode like "QQ==" but sets bits that no byte uses. */
    return compile_fails("var t: Text = base64(\"QR==\")\n",
                         "base64 non-canonical padding bits",
                         MESSAGE_CHECKER_BASE64_CANONICAL, (size_t)1);
}

TEST("Text only")
{
    return compile_fails("var s: String<8> = base64(\"SGk=\")\n",
                         "base64 into a fixed string",
                         MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "s",
                         "String<8>");
}

TEST("not comparable to a fixed string")
{
    return compile_fails("var s: String<8> = \"Hi\"\n"
                         "print(s == base64(\"SGk=\"))\n",
                         "base64 against a fixed string",
                         MESSAGE_CHECKER_OPERATOR_TYPE, "==");
}

TEST("reserved as a function")
{
    return compile_fails("fun base64(x: Int32): Int32\n  return x\nend\n",
                         "base64 as a function name",
                         MESSAGE_CHECKER_BUILTIN_RESERVED, "base64");
}

TEST("reserved as a parameter")
{
    return compile_fails("fun f(base64: Int32): Int32\n  return 1\nend\n",
                         "base64 as a parameter name",
                         MESSAGE_CHECKER_BUILTIN_RESERVED, "base64");
}

TEST("reserved as a variable")
{
    return compile_fails("var base64: Int32 = 3\n",
                         "base64 as a variable name",
                         MESSAGE_CHECKER_DUPLICATE_NAME, "base64");
}
