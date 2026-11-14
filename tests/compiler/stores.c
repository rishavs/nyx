/* Checker rules for Store, Handle, and alloc. */

#include "compile_support.h"

TEST("size must be constant")
{
    const char *source =
        "var n: Int32 = 4\n"
        "var s: Store = Store(n)\n";

    return compile_fails(source, "a non-constant store size",
                         MESSAGE_CHECKER_STORE_SIZE);
}

TEST("initializer must be Store(bytes)")
{
    return compile_fails("var s: Store = 16\n",
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
        "var s: Store = Store(16)\n"
        "var t = s\n";

    return compile_fails(source, "a store used as a value",
                         MESSAGE_CHECKER_STORE_NOT_VALUE, "s");
}

TEST("duplicate name")
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var s: Store = Store(16)\n";

    return compile_fails(source, "a duplicate store",
                         MESSAGE_CHECKER_DUPLICATE_NAME, "s");
}

TEST("parameter type")
{
    return compile_fails("fun f(x: Store): Int32\n  return 0\nend\n",
                         "a Store parameter",
                         MESSAGE_CHECKER_STORE_TYPE);
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
    return compile_fails("var s: Store<Int32> = Store(16)\n",
                         "type arguments on Store",
                         MESSAGE_CHECKER_TYPE_NOT_GENERIC, "Store");
}

TEST("alloc type argument")
{
    const char *source =
        "var s: Store = Store(16)\n"
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
        "var s: Store = Store(16)\n"
        "var p = s.alloc<Store>(nil)\n";

    return compile_fails(source, "alloc of a Store",
                         MESSAGE_CHECKER_HANDLE_FIXED);
}

TEST("alloc argument count")
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.alloc<Int32>()\n";

    return compile_fails(source, "alloc with no value",
                         MESSAGE_CHECKER_ALLOC_ARG_COUNT);
}

TEST("alloc value type")
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.alloc<Int32>(true)\n";

    return compile_fails(source, "alloc with the wrong value type",
                         MESSAGE_CHECKER_ALLOC_ARG_TYPE, "Int32");
}

TEST("handle element")
{
    const char *source =
        "var s: Store = Store(16)\n"
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

TEST("reset arguments")
{
    const char *source =
        "var s: Store = Store(16)\n"
        "s.reset(1)\n";

    return compile_fails(source, "reset with an argument",
                         MESSAGE_CHECKER_MEMBER_ARGS, "reset");
}

TEST("reset type argument")
{
    const char *source =
        "var s: Store = Store(16)\n"
        "s.reset<Int32>()\n";

    return compile_fails(source, "reset with a type argument",
                         MESSAGE_CHECKER_MEMBER_TYPE_ARG, "reset");
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
        "var s: Store = Store(16)\n"
        "var p: Handle<Int32> = s.alloc<Int32>(1)\n"
        "p.write(true)\n";

    return compile_fails(source, "write with the wrong value type",
                         MESSAGE_CHECKER_WRITE_TYPE);
}

TEST("unknown member")
{
    const char *source =
        "var s: Store = Store(16)\n"
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
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var h: Handle<Text> = arena.alloc<Text>(arena.text(f))\n",
        "alloc of a Text",
        MESSAGE_CHECKER_HANDLE_FIXED);
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
    return compile_fails(
        "type S\n"
        "  t: Text\n"
        "end\n"
        "global arena: Store = Store(64)\n"
        "var f: String<8> = \"x\"\n"
        "var t: Text = arena.text(f)\n"
        "var s: S = S(t)\n"
        "var h: Handle<S> = arena.alloc<S>(s)\n",
        "alloc of a struct with Text",
        MESSAGE_CHECKER_HANDLE_FIXED);
}

TEST("alloc of an Array with Text")
{
    return compile_fails(
        "global arena: Store = Store(64)\n"
        "var f: String<8> = \"x\"\n"
        "var t: Text = arena.text(f)\n"
        "var a: Array<Text, 1> = [t]\n"
        "var h: Handle<Array<Text, 1>> = "
        "arena.alloc<Array<Text, 1>>(a)\n",
        "alloc of an Array with Text",
        MESSAGE_CHECKER_HANDLE_FIXED);
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
