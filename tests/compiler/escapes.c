/* Escape analysis: a value from a scoped Store must not outlive its scope. */

#include "compile_support.h"
#include "alloc.h"

#include <stdio.h>
#include <string.h>

#define PRELUDE \
    "global root: Store = Store(256)\n" \
    "var names: List<Text> = root.list<Text>(4)\n"

TEST("escape: returning a scoped handle")
{
    return compile_fails(
        "fun leak(): Handle<Int32>\n"
        " scope s = Store(16) do\n  return s.alloc<Int32>(5)\n end\n"
        "end\n", "returned scoped handle", MESSAGE_ANALYSER_ESCAPE_RETURN, MESSAGE_FRAGMENT_STORE_PREFIX "s\'");
}

TEST("escape: returning a struct holding a scoped handle")
{
    return compile_fails(
        "type Box h: Handle<Int32> end\n"
        "fun mk(): Box\n scope s = Store(16) do\n"
        "  return Box(h = s.alloc<Int32>(1))\n end\nend\n",
        "returned scoped struct", MESSAGE_ANALYSER_ESCAPE_RETURN, MESSAGE_FRAGMENT_STORE_PREFIX "s\'");
}

TEST("escape: assigning a scoped value to an outer variable")
{
    return compile_fails(
        PRELUDE "var keep: Text = root.copy(\"k\")\n"
        "scope s = Store(64) do keep = s.copy(\"a\") end\n",
        "assignment to an outer variable", MESSAGE_ANALYSER_ESCAPE_ASSIGN,
        MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "keep");
}

TEST("escape: assigning from an inner scope to a middle scope")
{
    return compile_fails(
        "scope a = Store(64) do\n var mid: Text = a.copy(\"m\")\n"
        " scope b = Store(64) do mid = b.copy(\"d\") end\nend\n",
        "inner scope assigned outward", MESSAGE_ANALYSER_ESCAPE_ASSIGN,
        MESSAGE_FRAGMENT_STORE_PREFIX "b\'", "mid");
}

TEST("escape: assignment inside a loop")
{
    return compile_fails(
        PRELUDE "var cur: Text = root.copy(\"a\")\nvar i = 0\n"
        "while i < 3 do\n scope s = Store(64) do cur = s.copy(\"n\") end\n"
        " i = i + 1\nend\n", "loop assignment", MESSAGE_ANALYSER_ESCAPE_ASSIGN,
        MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "cur");
}

TEST("escape: pushing a scoped value into an outer list")
{
    return compile_fails(
        PRELUDE "scope s = Store(64) do names.push(s.copy(\"x\")) end\n",
        "push into an outer list", MESSAGE_ANALYSER_ESCAPE_WRITE,
                         MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "names");
}

TEST("escape: writing a scoped handle through an outer handle")
{
    return compile_fails(
        "global root: Store = Store(64)\n"
        "var hh = root.alloc<Handle<Int32>>(root.alloc<Int32>(1))\n"
        "scope s = Store(64) do hh.write(s.alloc<Int32>(2)) end\n",
        "handle write", MESSAGE_ANALYSER_ESCAPE_WRITE,
        MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "hh");
}

TEST("escape: writing a scoped value into an outer struct field")
{
    return compile_fails(
        "type Box h: Handle<Int32> end\n"
        "global root: Store = Store(64)\n"
        "var b = Box(h = root.alloc<Int32>(1))\n"
        "scope s = Store(64) do b.h = s.alloc<Int32>(2) end\n",
        "field write", MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "b");
}

TEST("escape: writing a scoped value into an outer array")
{
    return compile_fails(
        "global root: Store = Store(256)\n"
        "var a: Array<Text, 2> = [root.copy(\"a\"), root.copy(\"b\")]\n"
        "scope s = Store(64) do a[0] = s.copy(\"c\") end\n",
        "array write", MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "a");
}

TEST("escape: derived values keep their region")
{
    return compile_fails(
        PRELUDE "var keep: Text = root.copy(\"k\")\n"
        "scope s = Store(64) do\n var t = s.copy(\"hello\")\n"
        " keep = t.slice(1, 2)\nend\n", "slice of a scoped Text",
        MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "keep");
}

TEST("escape: reading from a scoped list keeps its region")
{
    return compile_fails(
        PRELUDE "var keep: Text = root.copy(\"k\")\n"
        "scope s = Store(64) do\n var l: List<Text> = s.list<Text>(2)\n"
        " l.push(s.copy(\"q\"))\n keep = l[0]\nend\n", "element read",
        MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "keep");
}

TEST("escape: for binding over a scoped list")
{
    return compile_fails(
        PRELUDE "var keep: Text = root.copy(\"k\")\n"
        "scope s = Store(64) do\n var l: List<Text> = s.list<Text>(2)\n"
        " l.push(s.copy(\"q\"))\n for t in l do keep = t end\nend\n",
        "for binding", MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "keep");
}

TEST("escape: if expression joins both branches")
{
    return compile_fails(
        PRELUDE "var keep: Text = root.copy(\"k\")\n"
        "scope s = Store(64) do\n"
        " keep = if true then s.copy(\"a\") else root.copy(\"b\") end\nend\n",
        "if expression", MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "keep");
}

TEST("escape: narrowed union keeps its region")
{
    return compile_fails(
        PRELUDE "var keep: Text = root.copy(\"k\")\n"
        "scope s = Store(64) do\n var u: Text | Nil = s.copy(\"a\")\n"
        " if typeof u is Text then keep = u end\nend\n", "narrowed union",
        MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "keep");
}

TEST("escape: a call that stores an argument into another")
{
    return compile_fails(
        "fun stash(l: List<Text>, t: Text) l.push(t) end\n" PRELUDE
        "scope s = Store(64) do stash(names, s.copy(\"x\")) end\n",
        "stashing call", MESSAGE_ANALYSER_ESCAPE_CALL, "stash", MESSAGE_FRAGMENT_STORE_PREFIX "s\'");
}

TEST("escape: a call that allocates in a Store parameter")
{
    return compile_fails(
        "fun add(s: Store, l: List<Text>) l.push(s.copy(\"x\")) end\n" PRELUDE
        "scope s = Store(64) do add(s, names) end\n",
        "Store parameter stash", MESSAGE_ANALYSER_ESCAPE_CALL, "add", MESSAGE_FRAGMENT_STORE_PREFIX "s\'");
}

TEST("escape: a call that allocates a value inside another Store")
{
    return compile_fails(
        "fun put(o: Store, t: Text): Handle<Text> return o.alloc<Text>(t) end\n"
        "global root: Store = Store(256)\n"
        "scope s = Store(64) do var h = put(root, s.copy(\"x\")) end\n",
        "alloc through a call", MESSAGE_ANALYSER_ESCAPE_CALL, "put", MESSAGE_FRAGMENT_STORE_PREFIX "s\'");
}

TEST("escape: the stash is found through a wrapper")
{
    return compile_fails(
        "fun stash(l: List<Text>, t: Text) l.push(t) end\n"
        "fun wrap(l: List<Text>, t: Text) stash(l, t) end\n" PRELUDE
        "scope s = Store(64) do wrap(names, s.copy(\"x\")) end\n",
        "transitive stash", MESSAGE_ANALYSER_ESCAPE_CALL, "wrap", MESSAGE_FRAGMENT_STORE_PREFIX "s\'");
}

TEST("escape: the stash is found through recursion")
{
    return compile_fails(
        "fun rec(l: List<Text>, t: Text, n: Int32)\n"
        " if n == 0 then l.push(t) else rec(l, t, n - 1) end\nend\n" PRELUDE
        "scope s = Store(64) do rec(names, s.copy(\"x\"), 2) end\n",
        "recursive stash", MESSAGE_ANALYSER_ESCAPE_CALL, "rec", MESSAGE_FRAGMENT_STORE_PREFIX "s\'");
}

TEST("escape: calls through a function value assume the worst")
{
    return compile_fails(
        "fun stash(l: List<Text>, t: Text) l.push(t) end\n"
        "fun run(f: Fun<(List<Text>, Text)>, l: List<Text>, t: Text) f(l, t) end\n"
        PRELUDE "scope s = Store(64) do run(stash, names, s.copy(\"x\")) end\n",
        "indirect call", MESSAGE_ANALYSER_ESCAPE_CALL, "run", MESSAGE_FRAGMENT_STORE_PREFIX "s\'");
}

TEST("escape: a call result carries its argument's region")
{
    return compile_fails(
        "fun pick(a: Text, b: Text): Text return a end\n" PRELUDE
        "var keep: Text = root.copy(\"k\")\n"
        "scope s = Store(64) do\n"
        " keep = pick(s.copy(\"x\"), root.copy(\"y\"))\nend\n",
        "returned argument", MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "s\'", "keep");
}

TEST("escape: a method that stores into self")
{
    return compile_fails(
        "type Holder t: Text end\n"
        "method Holder.set(v: Text): self\n self.t = v\n return self\nend\n"
        "global root: Store = Store(256)\n"
        "var h = Holder(root.copy(\"a\"))\n"
        "scope s = Store(64) do h.set(s.copy(\"b\")) end\n",
        "method write through self", MESSAGE_ANALYSER_ESCAPE_CALL,
                         MESSAGE_FRAGMENT_GENERATED_CALLEE, MESSAGE_FRAGMENT_STORE_PREFIX "s\'");
}

TEST("escape: the guide's rejected assignment names origin and destination")
{
    return compile_fails(
        "fun label(output: Store, input: Text): Text\n"
        " return output.concat(\"label: \", input)\nend\n"
        "scope output = Store(256) do\n"
        " var result: Text = \"\"\n"
        " scope scratch = Store(64) do\n"
        "  var input = scratch.copy(\"example\")\n"
        "  result = input\n"
        " end\nend\n", "guide example", MESSAGE_ANALYSER_ESCAPE_ASSIGN,
        MESSAGE_FRAGMENT_STORE_PREFIX "scratch\'", "result");
}

TEST("escape: a linked node from an inner scope is rejected in an outer one")
{
    return compile_fails(
        "type Node value: Int32 next: Handle<Node> | Nil end\n"
        "scope chain = Store(256) do\n"
        " scope inner = Store(256) do\n"
        "  var tail = inner.alloc<Node>(Node(2, nil))\n"
        "  var head = chain.alloc<Node>(Node(1, tail))\n"
        " end\nend\n", "guide linked example", MESSAGE_ANALYSER_ESCAPE_WRITE,
        MESSAGE_FRAGMENT_STORE_PREFIX "inner\'", "chain");
}

TEST("escape: a function that retains into global memory is not a value")
{
    return compile_fails(
        "global root: Store = Store(256)\n"
        "fun keep(t: Text): Handle<Text> return root.alloc<Text>(t) end\n"
        "var f: Fun<(Text):Handle<Text>> = keep\n"
        "scope s = Store(8) do end\n", "function value",
        MESSAGE_ANALYSER_ESCAPE_FUNCTION_VALUE, "keep");
}

TEST("scopes accept the guide's examples")
{
    return compile_accepts(
        "fun label(output: Store, input: Text): Text\n"
        " return output.concat(\"label: \", input)\nend\n"
        "type Node value: Int32 next: Handle<Node> | Nil end\n"
        "scope output = Store(256) do\n"
        " var result: Text = \"\"\n"
        " scope scratch = Store(64) do\n"
        "  var input = scratch.copy(\"example\")\n"
        "  result = label(output, input)\n"
        " end\n print(result)\nend\n"
        "scope chain = Store(256) do\n"
        " var tail = chain.alloc<Node>(Node(2, nil))\n"
        " var head = chain.alloc<Node>(Node(1, tail))\n"
        " print(head.read().value)\nend\n", "guide examples");
}

TEST("scopes accept a function value that only uses its arguments")
{
    return compile_accepts(
        "fun a(s: Store, t: Text): Text return s.copy(t) end\n"
        "fun use(f: Fun<(Store, Text):Text>, s: Store): Text return f(s, \"x\") end\n"
        "scope s = Store(64) do print(use(a, s)) end\n", "function value");
}

#define BATCH \
    "type Batch names: List<Text> end\n" \
    "fun add_name(batch: Batch, name: Text)\n" \
    " var l = batch.names\n l.push(name)\nend\n" \
    "global root: Store = Store(256)\n"

TEST("escape: a wrapped outer list and an inner Text meet in a call")
{
    return compile_fails(
        BATCH "var outer = Batch(root.list<Text>(4))\n"
        "scope inner = Store(64) do add_name(outer, inner.copy(\"x\")) end\n",
        "mixed aggregate call", MESSAGE_ANALYSER_ESCAPE_CALL, "add_name",
        MESSAGE_FRAGMENT_STORE_PREFIX "inner\'");
}

TEST("escape: the same write made directly is rejected despite inner Text")
{
    return compile_fails(
        "type Batch names: List<Text> tmp: Text end\n"
        "global root: Store = Store(256)\n"
        "scope inner = Store(64) do\n"
        " var b = Batch(root.list<Text>(4), inner.copy(\"t\"))\n"
        " var l = b.names\n l.push(inner.copy(\"x\"))\nend\n",
        "mixed aggregate direct write", MESSAGE_ANALYSER_ESCAPE_WRITE,
        MESSAGE_FRAGMENT_STORE_PREFIX "inner\'", "l");
}

TEST("scopes accept copying into the list's Store before the write")
{
    return compile_accepts(
        BATCH "var outer = Batch(root.list<Text>(4))\n"
        "scope inner = Store(64) do\n"
        " add_name(outer, root.copy(inner.copy(\"x\")))\nend\n",
        "copy then write");
}

TEST("scopes accept a wrapped list and Text from the same Store")
{
    return compile_accepts(
        BATCH "scope inner = Store(64) do\n"
        " var b = Batch(inner.list<Text>(4))\n"
        " add_name(b, inner.copy(\"x\"))\nend\n", "same Store");
}

#define MIXED \
    "type Batch names: List<Text> tmp: Text end\n" \
    "global root: Store = Store(256)\n" \
    "var keep: List<Text> = root.list<Text>(4)\n" \
    "var note: Text = root.copy(\"n\")\n"

TEST("scopes track struct fields separately")
{
    return compile_accepts(
        MIXED "scope inner = Store(64) do\n"
        " var b = Batch(root.list<Text>(4), inner.copy(\"t\"))\n"
        " keep = b.names\n"
        "end\n", "outer field of a mixed struct");
}

TEST("escape: the inner field of the same struct is still rejected")
{
    return compile_fails(
        MIXED "scope inner = Store(64) do\n"
        " var b = Batch(root.list<Text>(4), inner.copy(\"t\"))\n"
        " note = b.tmp\n"
        "end\n", "inner field of a mixed struct",
        MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "inner\'", "note");
}

TEST("scopes track a field written after construction")
{
    return compile_fails(
        MIXED "scope inner = Store(64) do\n"
        " var b = Batch(root.list<Text>(4), root.copy(\"t\"))\n"
        " b.tmp = inner.copy(\"x\")\n"
        " note = b.tmp\n"
        "end\n", "field overwritten with an inner value",
        MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "inner\'", "note");
}

TEST("scopes track nested struct paths")
{
    return compile_accepts(
        "type P a: Text b: Text end\n"
        "type Q p: P t: Text end\n"
        "global root: Store = Store(256)\n"
        "var note: Text = root.copy(\"n\")\n"
        "scope inner = Store(64) do\n"
        " var q = Q(P(root.copy(\"a\"), inner.copy(\"b\")), inner.copy(\"t\"))\n"
        " note = q.p.a\n"
        "end\n", "nested outer field");
}

TEST("escape: a nested inner path is rejected")
{
    return compile_fails(
        "type P a: Text b: Text end\n"
        "type Q p: P t: Text end\n"
        "global root: Store = Store(256)\n"
        "var note: Text = root.copy(\"n\")\n"
        "scope inner = Store(64) do\n"
        " var q = Q(P(root.copy(\"a\"), inner.copy(\"b\")), root.copy(\"t\"))\n"
        " note = q.p.b\n"
        "end\n", "nested inner field", MESSAGE_ANALYSER_ESCAPE_ASSIGN,
        MESSAGE_FRAGMENT_STORE_PREFIX "inner\'", "note");
}

TEST("scopes track union alternatives")
{
    return compile_accepts(
        "global root: Store = Store(256)\n"
        "var note: Text = root.copy(\"n\")\n"
        "scope inner = Store(64) do\n"
        " var u: Text | Handle<Int32> = root.copy(\"a\")\n"
        " if typeof u is Text then note = u end\n"
        " var w: Text | Handle<Int32> = inner.alloc<Int32>(1)\n"
        "end\n", "union alternatives are separate");
}

TEST("escape: a diagnostic notes where the value was allocated")
{
    return compile_fails(
        "global root: Store = Store(256)\n"
        "var note: Text = root.copy(\"n\")\n"
        "scope inner = Store(64) do note = inner.copy(\"x\") end\n",
        "allocation note", MESSAGE_ANALYSER_NOTE_ALLOCATED, "inner");
}

TEST("escape: a call diagnostic notes the retaining operation")
{
    return compile_fails(
        "fun stash(l: List<Text>, t: Text) l.push(t) end\n"
        "global root: Store = Store(256)\n"
        "var names: List<Text> = root.list<Text>(4)\n"
        "scope s = Store(64) do stash(names, s.copy(\"x\")) end\n",
        "retention note", MESSAGE_ANALYSER_NOTE_RETAINED, "stash");
}

TEST("escape: a call that keeps an argument in a global Store")
{
    return compile_fails(
        "global root: Store = Store(256)\n"
        "fun stash(t: Text)\n"
        "  var l: List<Text> = root.list<Text>(2)\n"
        "  l.push(t)\n"
        "end\n"
        "scope s = Store(64) do\n"
        "  stash(s.copy(\"x\"))\n"
        "end\n",
        "a callee that retains its argument in global memory",
        MESSAGE_ANALYSER_ESCAPE_CALL, "stash", MESSAGE_FRAGMENT_STORE_PREFIX "s\'");
}

#define FILL \
    "type Batch names: List<Text> tmp: Text end\n" \
    "fun fill(b: Batch, t: Text)\n var l = b.names\n l.push(t)\nend\n" \
    "global root: Store = Store(256)\n"

TEST("scopes check a call field by field")
{
    return compile_accepts(
        FILL "scope inner = Store(64) do\n"
        " fill(Batch(inner.list<Text>(4), root.copy(\"x\")), inner.copy(\"t\"))\n"
        "end\n", "the written field is in the inner Store");
}

TEST("escape: a call is still rejected when the written field is outer")
{
    return compile_fails(
        FILL "scope inner = Store(64) do\n"
        " fill(Batch(root.list<Text>(4), inner.copy(\"x\")), inner.copy(\"t\"))\n"
        "end\n", "the written field is global", MESSAGE_ANALYSER_ESCAPE_CALL,
        "fill", MESSAGE_FRAGMENT_STORE_PREFIX "inner\'");
}

#define SECOND \
    "type Pair a: Text b: Text end\n" \
    "fun second(p: Pair): Text return p.b end\n" \
    "global root: Store = Store(256)\n" \
    "var keep: Text = root.copy(\"k\")\n"

TEST("scopes follow the returned field of an argument")
{
    return compile_accepts(
        SECOND "scope inner = Store(64) do\n"
        " keep = second(Pair(inner.copy(\"a\"), root.copy(\"b\")))\n"
        "end\n", "the other field is returned");
}

TEST("escape: the returned field of an argument keeps its region")
{
    return compile_fails(
        SECOND "scope inner = Store(64) do\n"
        " keep = second(Pair(root.copy(\"a\"), inner.copy(\"b\")))\n"
        "end\n", "the returned field is inner",
        MESSAGE_ANALYSER_ESCAPE_ASSIGN, MESSAGE_FRAGMENT_STORE_PREFIX "inner\'", "keep");
}

TEST("scopes accept values that stay inside")
{
    return compile_accepts(
        "fun make(s: Store, n: Int32): Handle<Int32> return s.alloc<Int32>(n) end\n"
        "global root: Store = Store(256)\n"
        "var total: Int32 = 0\nvar keep: Text = root.copy(\"k\")\n"
        "scope s = Store(64) do\n"
        " var h = make(s, 5)\n var t = s.copy(\"hello\")\n"
        " total = total + h.read()\n"
        " keep = root.copy(t)\n"          /* bytes are copied into root */
        " print(h.read(), t, keep)\n"
        "end\n", "values kept inside their scope");
}

TEST("scopes accept shallower values written into deeper containers")
{
    return compile_accepts(
        "fun fill(l: List<Text>, t: Text) l.push(t) end\n"
        "global root: Store = Store(256)\n"
        "var outer: List<Text> = root.list<Text>(2)\n"
        "outer.push(root.copy(\"o\"))\n"
        "scope a = Store(256) do\n"
        " var l: List<Text> = a.list<Text>(4)\n"
        " fill(l, root.copy(\"r\"))\n fill(l, a.copy(\"a\"))\n"
        " l.push(outer[0])\n"
        " scope b = Store(64) do\n"
        "  var u: List<Text> = b.list<Text>(4)\n"
        "  u.push(l[0])\n  u.push(b.copy(\"y\"))\n"
        " end\n"
        "end\n", "shallow into deep");
}

TEST("scopes accept a function that returns what it was given")
{
    return compile_accepts(
        "fun first(l: List<Text>): Text return l[0] end\n"
        "fun swap(a: Text, b: Text): Text return b end\n"
        "global root: Store = Store(256)\n"
        "var g: List<Text> = root.list<Text>(2)\n"
        "g.push(root.copy(\"r\"))\nvar keep: Text = first(g)\n"
        "scope s = Store(64) do\n"
        " keep = swap(s.copy(\"x\"), first(g))\n"   /* only b is returned */
        "end\n", "result limited to returned parameters");
}

TEST("scopes accept values built in a Store parameter")
{
    return compile_accepts(
        "fun mk(s: Store): List<Text>\n"
        " var l: List<Text> = s.list<Text>(2)\n l.push(s.copy(\"z\"))\n"
        " return l\nend\n"
        "scope s = Store(256) do var l = mk(s) print(l[0]) end\n",
        "list built in a Store parameter");
}

TEST("scopes accept writes into a container a call built in the caller's Store")
{
    return compile_accepts(
        "fun mk(s: Store): List<Text> return s.list<Text>(4) end\n"
        "scope s = Store(256) do\n var l = mk(s)\n l.push(s.copy(\"a\"))\n"
        " print(l[0])\nend\n", "container returned from a call");
}

TEST("escape analysis fails closed when an allocation fails")
{
    const char *source =
        "fun id(t: Text): Text return t end\n"
        "fun put(l: List<Text>, t: Text) l.push(id(t)) end\n"
        "global root: Store = Store(256)\n"
        "var names: List<Text> = root.list<Text>(4)\n"
        "scope s = Store(64) do put(names, root.copy(\"x\")) end\n";
    CompilerResult baseline;
    size_t count, i;

    nyx_alloc_reset();
    nyx_alloc_begin_counting();
    baseline = compiler_compile(source, &test_config);
    count = nyx_alloc_attempts();
    nyx_alloc_reset();
    if (!baseline.succeeded) {
        fprintf(stderr, "  baseline did not compile: %s\n",
                baseline.error_message);
        compiler_result_free(&baseline);
        return TEST_ERROR;
    }
    for (i = 0; i < count; i++) {
        CompilerResult result;
        bool valid;

        nyx_alloc_fail_after(i);
        result = compiler_compile(source, &test_config);
        nyx_alloc_reset();
        valid = result.succeeded
                    ? result.c_source != NULL &&
                          strcmp(result.c_source, baseline.c_source) == 0
                    : result.c_source == NULL && result.error_message != NULL;
        compiler_result_free(&result);
        if (!valid) {
            fprintf(stderr, "  invalid result after allocation %zu of %zu\n",
                    i, count);
            compiler_result_free(&baseline);
            return TEST_FAIL;
        }
    }
    compiler_result_free(&baseline);
    return TEST_PASS;
}

TEST("scopes accept a collection kept inside its own scope")
{
    return compile_accepts(
        "scope s = Store(256) do\n"
        " var a: Array<Text, 2> = [s.copy(\"a\"), s.copy(\"b\")]\n"
        " a[0] = s.copy(\"c\")\n"
        " var r: Record<String<8>, Text, 4> = {}\n"
        " r[\"k\"] = s.copy(\"v\")\n"
        " print(a[0], r[\"k\"])\n"
        "end\n", "arrays and records inside a scope");
}

TEST("scopes accept a function with its own scope that returns a plain value")
{
    return compile_accepts(
        "fun f(n: Int32): Int32\n scope s = Store(16) do\n"
        "  var h = s.alloc<Int32>(n)\n  return h.read()\n end\nend\n"
        "print(f(4))\n", "scope inside a function");
}

