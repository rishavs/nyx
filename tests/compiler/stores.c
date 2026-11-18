/* Checker rules for Store, Handle, and alloc. */

#include "compile_support.h"

TEST("size must be constant")
{
    const char *source =
        "var n: Int32 = 4\n"
        "global s: Store = Store(n)\n";

    return compile_fails(source, "a non-constant store size",
                         MESSAGE_CHECKER_STORE_SIZE);
}

TEST("initializer must be Store(bytes)")
{
    return compile_fails("global s: Store = 16\n",
                         "a store without Store(bytes)",
                         MESSAGE_CHECKER_STORE_INITIALIZER, "s");
}

TEST("missing annotation")
{
    return compile_fails("var s = Store(16)\n",
                         "Store(...) without a Store annotation",
                         MESSAGE_CHECKER_STORE_TYPE);
}

TEST("not a value")
{
    const char *source =
        "global s: Store = Store(16)\n"
        "var t = s\n";

    return compile_fails(source, "a store used as a value",
                         MESSAGE_CHECKER_STORE_NOT_VALUE, "s");
}

TEST("duplicate name")
{
    const char *source =
        "global s: Store = Store(16)\n"
        "global s: Store = Store(16)\n";

    return compile_fails(source, "a duplicate store",
                         MESSAGE_CHECKER_DUPLICATE_NAME, "s");
}

TEST("parameter type")
{
    return compile_accepts("fun f(x: Store): Int32\n  return 0\nend\n",
                           "a Store parameter");
}

TEST("return type")
{
    return compile_fails("fun f(): Store\n  return 0\nend\n",
                         "a Store return type",
                         MESSAGE_CHECKER_STORE_TYPE);
}

TEST("struct field")
{
    return compile_fails("type H\n  value: Store\nend\n",
                         "a Store struct field",
                         MESSAGE_CHECKER_FIELD_TYPE, "value");
}

TEST("union alternative")
{
    return compile_fails("type S = Store | Nil\n",
                         "a Store union alternative",
                         MESSAGE_CHECKER_UNION_FUNCTION);
}

TEST("type arguments")
{
    return compile_fails("global s: Store<Int32> = Store(16)\n",
                         "type arguments on Store",
                         MESSAGE_CHECKER_TYPE_NOT_GENERIC, "Store");
}

TEST("alloc type argument")
{
    const char *source =
        "global s: Store = Store(16)\n"
        "var p = s.alloc(1)\n";

    return compile_fails(source, "alloc without a type argument",
                         MESSAGE_CHECKER_ALLOC_TYPE_ARG);
}

TEST("Handle argument count")
{
    return compile_fails(
        "var h: Handle<Int32, Bool> = 0\n",
        "Handle type argument count",
        MESSAGE_CHECKER_HANDLE_ELEMENT);
}

TEST("alloc Store")
{
    const char *source =
        "global s: Store = Store(16)\n"
        "var p = s.alloc<Store>(nil)\n";

    return compile_fails(source, "alloc of a Store",
                         MESSAGE_CHECKER_HANDLE_FIXED);
}

TEST("alloc argument count")
{
    const char *source =
        "global s: Store = Store(16)\n"
        "var p = s.alloc<Int32>()\n";

    return compile_fails(source, "alloc with no value",
                         MESSAGE_CHECKER_ALLOC_ARG_COUNT);
}

TEST("alloc value type")
{
    const char *source =
        "global s: Store = Store(16)\n"
        "var p = s.alloc<Int32>(true)\n";

    return compile_fails(source, "alloc with the wrong value type",
                         MESSAGE_CHECKER_ALLOC_ARG_TYPE, "Int32");
}

TEST("handle element")
{
    const char *source =
        "global s: Store = Store(16)\n"
        "var h: Handle<Store> = s.alloc<Int32>(1)\n";

    return compile_fails(source, "a Store handle element",
                         MESSAGE_CHECKER_HANDLE_FIXED);
}

TEST("unknown store")
{
    return compile_fails("var p = nope.alloc<Int32>(1)\n",
                         "an unknown store",
                         MESSAGE_CHECKER_UNKNOWN_STORE, "nope");
}

TEST("reset no longer exists")
{
    const char *source =
        "global s: Store = Store(16)\n"
        "s.reset()\n";

    return compile_fails(source, "reset on a Store",
                         MESSAGE_CHECKER_UNKNOWN_MEMBER, "reset");
}

TEST("a Store declared with var is rejected")
{
    return compile_fails("var s: Store = Store(16)\n",
                         "a var Store declaration",
                         MESSAGE_CHECKER_STORE_NOT_GLOBAL, "s");
}

TEST("read on non-handle")
{
    const char *source =
        "var x: Int32 = 1\n"
        "var y: Int32 = x.read()\n";

    return compile_fails(source, "read on an Int32",
                         MESSAGE_CHECKER_MEMBER_RECEIVER, "read");
}

TEST("write type")
{
    const char *source =
        "global s: Store = Store(16)\n"
        "var p: Handle<Int32> = s.alloc<Int32>(1)\n"
        "p.write(true)\n";

    return compile_fails(source, "write with the wrong value type",
                         MESSAGE_CHECKER_WRITE_TYPE);
}

TEST("unknown member")
{
    const char *source =
        "global s: Store = Store(16)\n"
        "var p = s.foo()\n";

    return compile_fails(source, "an unknown member",
                         MESSAGE_CHECKER_UNKNOWN_MEMBER, "foo");
}

TEST("global declaration")
{
    return compile_fails(
        "global x: Int32 = 1\n",
        "a global declaration that is not a Store",
        MESSAGE_CHECKER_GLOBAL_DECLARATION, "x");
}

TEST("global initializer")
{
    return compile_fails(
        "global arena: Store = 1\n",
        "a global Store with a non-Store initializer",
        MESSAGE_CHECKER_STORE_INITIALIZER, "arena");
}

TEST("handle of a collection")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var h: Handle<List<Int32>> = "
        "arena.alloc<List<Int32>>(arena.list<Int32>(1))\n",
        "handle of a collection",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TEST("alloc of a Text")
{
    return compile_accepts(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var h: Handle<Text> = arena.alloc<Text>(arena.text(f))\n",
        "alloc of a Text");
}

TEST("handle of a builder")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var h: Handle<TextBuilder> = "
        "arena.alloc<TextBuilder>(arena.builder(1))\n",
        "handle of a builder",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TEST("alloc of a struct with Text")
{
    return compile_accepts(
        "type S\n"
        "  t: Text\n"
        "end\n"
        "global arena: Store = Store(64)\n"
        "var f: String<8> = \"x\"\n"
        "var t: Text = arena.text(f)\n"
        "var s: S = S(t)\n"
        "var h: Handle<S> = arena.alloc<S>(s)\n",
        "alloc of a struct with Text");
}

TEST("alloc of an Array with Text")
{
    return compile_accepts(
        "global arena: Store = Store(64)\n"
        "var f: String<8> = \"x\"\n"
        "var t: Text = arena.text(f)\n"
        "var a: Array<Text, 1> = [t]\n"
        "var h: Handle<Array<Text, 1>> = "
        "arena.alloc<Array<Text, 1>>(a)\n",
        "alloc of an Array with Text");
}

TEST("Handle Record")
{
    return compile_fails(
        "var x: Handle<Record<String<8>, Bool, 4>> = 0\n",
        "Handle Record",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TEST("alloc Record")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var x: Handle<Record<String<8>, Bool, 4>> = "
        "arena.alloc<Record<String<8>, Bool, 4>>({})\n",
        "alloc Record",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TEST("Store capability cannot be copied")
{
    return compile_fails("fun f(s: Store) var x = s end\n",
                         "Store copied to a variable",
                         MESSAGE_CHECKER_STORE_NOT_VALUE, "s");
}

TEST("Store capability cannot be inferred as a generic type")
{
    return compile_fails("fun id<T>(x: T): T return x end\n"
                         "global arena: Store = Store(1)\n"
                         "id(arena)\n", "Store inferred as T",
                         MESSAGE_CHECKER_STORE_NOT_VALUE, "arena");
}

TEST("function types cannot return a Store")
{
    return compile_fails("fun f(g: Fun<(): Store>) end\n",
                         "Store as function value return type",
                         MESSAGE_CHECKER_STORE_TYPE);
}

TEST("Store capability cannot be a generic type argument")
{
    return compile_fails("type Box<T> x: Int32 end\n"
                         "var b: Box<Store> = Box(1)\n",
                         "Store as a type argument", MESSAGE_CHECKER_STORE_TYPE);
}

TEST("Store cannot be inferred through a function type")
{
    return compile_fails("fun use(s: Store) end\n"
                         "fun accept<T>(f: Fun<(T)>) end\n"
                         "accept(use)\n", "Store inferred through Fun",
                         MESSAGE_CHECKER_STORE_TYPE);
}

TEST("Store parameters cannot be assigned")
{
    return compile_fails("fun f(s: Store, t: Store) s = t end\n",
                         "assignment to a Store parameter",
                         MESSAGE_CHECKER_CONST_ASSIGN, "s");
}

TEST("a constant Store capacity folds every integer operator")
{
    return compile_accepts(
        "global a: Store = Store(3 * 4 + 20 / 4 - 7 % 4)\n"
        "global b: Store = Store(6 & 3 | 8 ^ 1)\n"
        "global c: Store = Store(1 << 4 >> 2)\n"
        "global d: Store = Store(((0 - 2147483647 - 1) % (0 - 1)) + 4)\n"
        "global e: Store = Store(-(0 - 16))\n",
        "folded capacities");
}

TEST("a constant Store capacity reports what cannot fold")
{
    TestResult divide = compile_fails("global s: Store = Store(8 / 0)\n",
                                      "a zero divisor in a capacity",
                                      MESSAGE_CHECKER_ZERO_DIVISOR, "/");
    TestResult modulo;
    TestResult overflow;
    TestResult shift;
    TestResult negate;

    if (divide != TEST_PASS) return divide;
    modulo = compile_fails("global s: Store = Store(8 % 0)\n",
                           "a zero modulus in a capacity",
                           MESSAGE_CHECKER_ZERO_DIVISOR, "%");
    if (modulo != TEST_PASS) return modulo;
    overflow = compile_fails(
        "global s: Store = Store((0 - 2147483647 - 1) / (0 - 1))\n",
        "the smallest Int32 divided by -1", MESSAGE_CHECKER_CONST_OVERFLOW);
    if (overflow != TEST_PASS) return overflow;
    shift = compile_fails("global s: Store = Store(1 << 32)\n",
                          "a shift past the width", MESSAGE_CHECKER_SHIFT_RANGE);
    if (shift != TEST_PASS) return shift;
    negate = compile_fails(
        "global s: Store = Store(-(0 - 2147483647 - 1))\n",
        "negating the smallest Int32", MESSAGE_CHECKER_CONST_OVERFLOW);
    if (negate != TEST_PASS) return negate;
    return compile_fails("global s: Store = Store(2147483647 + 1)\n",
                         "an overflowing sum", MESSAGE_CHECKER_CONST_OVERFLOW);
}
