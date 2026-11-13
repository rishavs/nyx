/* Checker rules for Array, List, Slice, Record, and Table. */

#include "compile_support.h"

TEST("list: dynamic list literal")
{
    return compile_fails(
        "var xs: List<Int32> = [1]\n",
        "dynamic list literal",
        MESSAGE_CHECKER_LIST_LITERAL_CONTEXT);
}

TEST("list: empty inferred literal")
{
    return compile_fails(
        "var xs = []\n",
        "empty inferred list literal",
        MESSAGE_CHECKER_LIST_INFER);
}

TEST("list: fixed list over capacity")
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2, 3]\n",
        "fixed list over capacity",
        MESSAGE_CHECKER_LIST_LITERAL_LENGTH, 3, 2);
}

TEST("list: list element not sized")
{
    return compile_fails(
        "var xs: Array<Store, 1> = []\n",
        "list element not sized",
        MESSAGE_CHECKER_ARRAY_ELEMENT);
}

TEST("list: bare List")
{
    return compile_fails(
        "var xs: List = []\n",
        "bare List",
        MESSAGE_CHECKER_LIST_ARGS);
}

TEST("list: list capacity too large")
{
    return compile_fails(
        "var xs: Array<Int32, 4097> = []\n",
        "list capacity too large",
        MESSAGE_CHECKER_ARRAY_CAPACITY, NIPPY_DEFAULT_MAX_LIST_CAPACITY);
}

TEST("list: list index type")
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1]\nvar n: Int32 = xs[\"x\"]\n",
        "list index type",
        MESSAGE_CHECKER_LIST_INDEX);
}

TEST("list: list value type")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list<Int32>(1)\n"
        "xs.push(true)\n",
        "list value type",
        MESSAGE_CHECKER_LIST_VALUE);
}

TEST("list: list create type argument")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list(1)\n",
        "list create type argument",
        MESSAGE_CHECKER_LIST_TYPE_ARG);
}

TEST("list: list create argument count")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list<Int32>()\n",
        "list create argument count",
        MESSAGE_CHECKER_LIST_CREATE_ARGS);
}

TEST("list: list capacity argument type")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list<Int32>(\"x\")\n",
        "list capacity argument type",
        MESSAGE_CHECKER_LIST_CAPACITY_ARG);
}

TEST("list: push temporary list")
{
    return compile_fails(
        "global arena: Store = Store(16)\narena.list<Int32>(1).push(1)\n",
        "push temporary list",
        MESSAGE_CHECKER_LIST_LVALUE);
}

TEST("list: index type")
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1]\nvar n: Int32 = xs[\"x\"]\n",
        "index type",
        MESSAGE_CHECKER_LIST_INDEX);
}

TEST("slice: slice fixed list")
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1]\nvar s: Slice<Int32> = xs.slice(0, 1)\n",
        "slice fixed list",
        MESSAGE_CHECKER_SLICE_DYNAMIC);
}

TEST("slice: slice index write")
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list<Int32>(1)\n"
        "xs.push(1)\n"
        "var s: Slice<Int32> = xs.slice(0, 1)\n"
        "s[0] = 2\n",
        "slice index write",
        MESSAGE_CHECKER_SLICE_READONLY);
}

TEST("slice: bare Slice")
{
    return compile_fails(
        "var s: Slice = []\n",
        "bare Slice",
        MESSAGE_CHECKER_SLICE_ARGS);
}

TEST("slice: Slice element type")
{
    return compile_fails(
        "var s: Slice<Store> = nil\n",
        "Slice element type",
        MESSAGE_CHECKER_SLICE_ELEMENT);
}

TEST("List of List")
{
    return compile_fails(
        "var xs: List<List<Int32>> = []\n",
        "List of List",
        MESSAGE_CHECKER_COLLECTION_ELEMENT);
}

TEST("Array of Array")
{
    return compile_fails(
        "var xs: Array<Array<Int32, 1>, 1> = []\n",
        "Array of Array",
        MESSAGE_CHECKER_ARRAY_ELEMENT);
}

TEST("bare Array")
{
    return compile_fails(
        "var xs: Array = []\n",
        "bare Array",
        MESSAGE_CHECKER_ARRAY_ARGS);
}

TEST("List of Slice")
{
    return compile_fails(
        "var xs: List<Slice<Int32>> = []\n",
        "List of Slice",
        MESSAGE_CHECKER_COLLECTION_ELEMENT);
}

TEST("Record literal context")
{
    return compile_fails(
        "var x = {\"a\": true}\n",
        "Record literal context",
        MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
}

TEST("Record duplicate key")
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 4> = {\"a\": true, \"a\": false}\n",
        "Record duplicate key",
        MESSAGE_CHECKER_RECORD_DUPLICATE_KEY);
}

TEST("Record key type")
{
    return compile_fails(
        "var x: Record<Int32, Bool, 4> = {}\n",
        "Record key type",
        MESSAGE_CHECKER_MAP_KEY);
}

TEST("Record value collection")
{
    return compile_fails(
        "var x: Record<String<8>, List<Int32>, 4> = {}\n",
        "Record value collection",
        MESSAGE_CHECKER_MAP_VALUE);
}

TEST("Record capacity")
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 4097> = {}\n",
        "Record capacity",
        MESSAGE_CHECKER_RECORD_CAPACITY);
}

TEST("Record literal capacity")
{
    return compile_fails(
        "var x: Record<String<8>, Bool, 1> = {\"a\": true, \"b\": false}\n",
        "Record literal capacity",
        MESSAGE_CHECKER_RECORD_CAPACITY);
}

TEST("Table key type")
{
    return compile_fails(
        "var x: Table<Text, Bool> = arena.table<Text, Bool>(1)\n",
        "Table key type",
        MESSAGE_CHECKER_MAP_KEY);
}

TEST("Table literal")
{
    return compile_fails(
        "var x: Table<String<8>, Bool> = {}\n",
        "Table literal",
        MESSAGE_CHECKER_RECORD_LITERAL_CONTEXT);
}

TEST("bare Record")
{
    return compile_fails(
        "var x: Record = {}\n",
        "bare Record",
        MESSAGE_CHECKER_RECORD_ARGS);
}

TEST("bare Table")
{
    return compile_fails(
        "var x: Table = arena.table(1)\n",
        "bare Table",
        MESSAGE_CHECKER_TABLE_ARGS);
}
