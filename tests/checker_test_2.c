#include "test_support.h"
#include "golden_data.h"


TestResult test_checker_duplicate_store(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var s: Store = Store(16)\n";

    return compile_fails(source, "a duplicate store",
                         MESSAGE_CHECKER_DUPLICATE_NAME, "s");
}


TestResult test_checker_store_parameter(void)
{
    return compile_fails("fun f(x: Store): Int32\n  return 0\nend\n",
                         "a Store parameter",
                         MESSAGE_CHECKER_STORE_TYPE);
}


TestResult test_checker_store_return(void)
{
    return compile_fails("fun f(): Store\n  return 0\nend\n",
                         "a Store return type",
                         MESSAGE_CHECKER_STORE_TYPE);
}


TestResult test_checker_store_field(void)
{
    return compile_fails("type H\n  value: Store\nend\n",
                         "a Store struct field",
                         MESSAGE_CHECKER_FIELD_TYPE, "value");
}


TestResult test_checker_store_union(void)
{
    return compile_fails("type S = Store | Nil\n",
                         "a Store union alternative",
                         MESSAGE_CHECKER_UNION_FUNCTION);
}


TestResult test_checker_store_type_arguments(void)
{
    return compile_fails("var s: Store<Int32> = Store(16)\n",
                         "type arguments on Store",
                         MESSAGE_CHECKER_TYPE_NOT_GENERIC, "Store");
}


TestResult test_checker_alloc_type_argument(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.alloc(1)\n";

    return compile_fails(source, "alloc without a type argument",
                         MESSAGE_CHECKER_ALLOC_TYPE_ARG);
}


TestResult test_checker_handle_args(void)
{
    return compile_fails(
        "var h: Handle<Int32, Bool> = 0\n",
        "Handle type argument count",
        MESSAGE_CHECKER_HANDLE_ELEMENT);
}


TestResult test_checker_alloc_store(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.alloc<Store>(nil)\n";

    return compile_fails(source, "alloc of a Store",
                         MESSAGE_CHECKER_HANDLE_FIXED);
}


TestResult test_checker_alloc_count(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.alloc<Int32>()\n";

    return compile_fails(source, "alloc with no value",
                         MESSAGE_CHECKER_ALLOC_ARG_COUNT);
}


TestResult test_checker_alloc_value_type(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.alloc<Int32>(true)\n";

    return compile_fails(source, "alloc with the wrong value type",
                         MESSAGE_CHECKER_ALLOC_ARG_TYPE, "Int32");
}


TestResult test_checker_handle_element(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var h: Handle<Store> = s.alloc<Int32>(1)\n";

    return compile_fails(source, "a Store handle element",
                         MESSAGE_CHECKER_HANDLE_FIXED);
}


TestResult test_checker_unknown_store(void)
{
    return compile_fails("var p = nope.alloc<Int32>(1)\n",
                         "an unknown store",
                         MESSAGE_CHECKER_UNKNOWN_STORE, "nope");
}


TestResult test_checker_reset_args(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var r: Nil = s.reset(1)\n";

    return compile_fails(source, "reset with an argument",
                         MESSAGE_CHECKER_MEMBER_ARGS, "reset");
}


TestResult test_checker_reset_type_argument(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var r: Nil = s.reset<Int32>()\n";

    return compile_fails(source, "reset with a type argument",
                         MESSAGE_CHECKER_MEMBER_TYPE_ARG, "reset");
}


TestResult test_checker_read_on_non_handle(void)
{
    const char *source =
        "var x: Int32 = 1\n"
        "var y: Int32 = x.read()\n";

    return compile_fails(source, "read on an Int32",
                         MESSAGE_CHECKER_MEMBER_RECEIVER, "read");
}


TestResult test_checker_write_type(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p: Handle<Int32> = s.alloc<Int32>(1)\n"
        "var w: Nil = p.write(true)\n";

    return compile_fails(source, "write with the wrong value type",
                         MESSAGE_CHECKER_WRITE_TYPE);
}


TestResult test_checker_byte_range(void)
{
    return compile_fails("var b: Byte = 300\n",
                         "a byte literal out of range",
                         MESSAGE_CHECKER_BYTE_RANGE, 300);
}


TestResult test_checker_unknown_member(void)
{
    const char *source =
        "var s: Store = Store(16)\n"
        "var p = s.foo()\n";

    return compile_fails(source, "an unknown member",
                         MESSAGE_CHECKER_UNKNOWN_MEMBER, "foo");
}


TestResult test_checker_dynamic_string(void)
{
    return compile_fails(
        "var s: Text = \"x\"\nvar f: String<8> = s\n",
        "a dynamic Text assigned to a fixed Text",
        MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "f", "String<8>");
}


TestResult test_checker_string_too_long(void)
{
    return compile_fails(
        "var s: String<4> = \"hello\"\n",
        "a string literal larger than its capacity",
        MESSAGE_CHECKER_STRING_TOO_LONG, (size_t)5, 4);
}


TestResult test_checker_string_literal_context(void)
{
    return compile_fails(
        "var s = \"x\"\n",
        "a string literal without a String<N> context",
        MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
}


TestResult test_checker_string_compare_context(void)
{
    return compile_fails(
        "var ok: Bool = \"a\" == \"b\"\n",
        "comparing two uncontextual string literals",
        MESSAGE_CHECKER_STRING_COMPARE_CONTEXT);
}


TestResult test_checker_string_ambiguous(void)
{
    return compile_fails(
        "var u: String<4> | String<8> = \"hi\"\n",
        "a literal that fits two union alternatives",
        MESSAGE_CHECKER_STRING_AMBIGUOUS);
}


TestResult test_checker_string_capacity(void)
{
    return compile_fails(
        "var s: String<4097> = \"\"\n",
        "a Text capacity above the limit",
        MESSAGE_CHECKER_STRING_CAPACITY, 4096);
}


TestResult test_checker_string_assignment(void)
{
    return compile_fails(
        "var a: String<8> = \"hi\"\nvar b: String<16> = a\n",
        "assigning a String<8> to a String<16>",
        MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "b", "String<16>");
}


TestResult test_checker_string_equality(void)
{
    return compile_fails(
        "var a: String<8> = \"hi\"\nvar b: String<16> = \"hi\"\nvar ok: Bool "
        "= a == b\n",
        "comparing different Text capacities",
        MESSAGE_CHECKER_OPERATOR_TYPE, "==");
}


TestResult test_checker_string_len_args(void)
{
    return compile_fails(
        "var s: String<8> = \"hi\"\nvar n: Int32 = s.byte_len(1)\n",
        "byte_len with an argument",
        MESSAGE_CHECKER_MEMBER_ARGS, "byte_len");
}


TestResult test_checker_string_index_type(void)
{
    return compile_fails(
        "var s: String<8> = \"hi\"\nvar b: Byte = s.byte_at(\"x\")\n",
        "byte_at with a string index",
        MESSAGE_CHECKER_STRING_INDEX);
}


TestResult test_checker_string_method_type_arg(void)
{
    return compile_fails(
        "var s: String<8> = \"hi\"\nvar n: Int32 = s.byte_len<String<8>>()\n",
        "a type argument on byte_len",
        MESSAGE_CHECKER_MEMBER_TYPE_ARG, "byte_len");
}


TestResult test_checker_string_receiver(void)
{
    return compile_fails(
        "var n: Int32 = 1.byte_len()\n",
        "byte_len on an Int32",
        MESSAGE_CHECKER_MEMBER_RECEIVER, "byte_len");
}


TestResult test_checker_string_literal_receiver(void)
{
    return compile_fails(
        "var n: Int32 = \"hi\".byte_len()\n",
        "byte_len on an uncontextual literal",
        MESSAGE_CHECKER_STRING_LITERAL_CONTEXT);
}


TestResult test_checker_error_args(void)
{
    return compile_fails(
        "var e: Error = Error(\"E\", \"d\")\n",
        "Error with too few arguments",
        MESSAGE_CHECKER_ERROR_ARGS);
}


TestResult test_checker_error_arg_type(void)
{
    return compile_fails(
        "var e: Error = Error(\"E\", 1, \"f\", 1, 2, 3)\n",
        "Error with a non-string description",
        MESSAGE_CHECKER_ERROR_ARG_TYPE, (size_t)2);
}


TestResult test_checker_error_named_args(void)
{
    return compile_fails(
        "var e: Error = Error(header=\"E\", description=\"d\", "
        "filename=\"f\", line=1, col=2, pos=3)\n",
        "Error with named arguments",
        MESSAGE_CHECKER_ERROR_ARGS);
}


TestResult test_checker_error_member(void)
{
    return compile_fails(
        "var n: Int32 = 1.line()\n",
        "an Error field method on an Int32",
        MESSAGE_CHECKER_MEMBER_RECEIVER, "line");
}


TestResult test_checker_redefines_string(void)
{
    return compile_fails(
        "type String = Int32\n",
        "redefining String",
        MESSAGE_CHECKER_REDEFINES_STRING);
}


TestResult test_checker_redefines_text(void)
{
    return compile_fails(
        "type Text = Int32\n",
        "redefining Text",
        MESSAGE_CHECKER_REDEFINES_TEXT);
}


TestResult test_checker_redefines_error(void)
{
    return compile_fails(
        "type Error = Int32\n",
        "redefining Error",
        MESSAGE_CHECKER_REDEFINES_ERROR);
}


TestResult test_checker_string_variable_name(void)
{
    return compile_fails(
        "var Text: Int32 = 1\n",
        "a variable named Text",
        MESSAGE_CHECKER_DUPLICATE_NAME, "Text");
}



TestResult test_checker_assign_unknown(void)
{
    return compile_fails(
        "x = 1\n",
        "an assignment to an unknown name",
        MESSAGE_CHECKER_UNKNOWN_NAME, "x");
}


TestResult test_checker_assign_store(void)
{
    return compile_fails(
        "var s: Store = Store(8)\ns = Store(8)\n",
        "an assignment to a store",
        MESSAGE_CHECKER_ASSIGN_TARGET, "s");
}


TestResult test_checker_assign_function(void)
{
    return compile_fails(
        "fun f(): Nil\n  return nil\nend\nf = 1\n",
        "an assignment to a function",
        MESSAGE_CHECKER_ASSIGN_TARGET, "f");
}


TestResult test_checker_assign_type(void)
{
    return compile_fails(
        "type P\n  x: Int32\nend\nP = 1\n",
        "an assignment to a type",
        MESSAGE_CHECKER_ASSIGN_TARGET, "P");
}


TestResult test_checker_assign_builtin(void)
{
    return compile_fails(
        "Int32 = 1\n",
        "an assignment to a built-in type",
        MESSAGE_CHECKER_ASSIGN_TARGET, "Int32");
}


TestResult test_checker_assign_global_in_function(void)
{
    return compile_fails(
        "var n: Int32 = 1\nfun f(): Nil\n  n = 2\n  return nil\nend\n",
        "assigning a program-level variable inside a function",
        MESSAGE_CHECKER_GLOBAL_IN_FUNCTION, "n");
}


TestResult test_checker_assign_type_mismatch(void)
{
    return compile_fails(
        "var n: Int32 = 1\nn = true\n",
        "assigning a Bool to an Int32",
        MESSAGE_CHECKER_ASSIGN_TYPE, "n");
}


TestResult test_checker_assign_byte_range(void)
{
    return compile_fails(
        "var b: Byte = 1\nb = 300\n",
        "assigning an out-of-range Byte literal",
        MESSAGE_CHECKER_BYTE_RANGE, 300);
}


TestResult test_checker_assign_string_too_long(void)
{
    return compile_fails(
        "var s: String<4> = \"a\"\ns = \"hello\"\n",
        "assigning a literal larger than the destination",
        MESSAGE_CHECKER_STRING_TOO_LONG, (size_t)5, 4);
}



TestResult test_checker_global_declaration(void)
{
    return compile_fails(
        "global x: Int32 = 1\n",
        "a global declaration that is not a Store",
        MESSAGE_CHECKER_GLOBAL_DECLARATION, "x");
}


TestResult test_checker_global_store_initializer(void)
{
    return compile_fails(
        "global arena: Store = 1\n",
        "a global Store with a non-Store initializer",
        MESSAGE_CHECKER_STORE_INITIALIZER, "arena");
}


TestResult test_checker_concat_type(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var s: Text = arena.concat(f, \"y\")\n",
        "concat with a fixed string",
        MESSAGE_CHECKER_CONCAT_TYPE);
}


TestResult test_checker_copy_type(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var s: Text = arena.copy(f)\n",
        "copy with a fixed string",
        MESSAGE_CHECKER_COPY_TYPE);
}


TestResult test_checker_store_string_type(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\nvar s: Text = arena.text(\"hi\")\n",
        "store.text with a dynamic literal",
        MESSAGE_CHECKER_STORE_TEXT_TYPE);
}


TestResult test_checker_slice_type(void)
{
    return compile_fails(
        "var s: Text = \"hello\"\nvar t: Text = s.slice(\"x\", 1)\n",
        "slice with a non-Int32 start",
        MESSAGE_CHECKER_STRING_SLICE_TYPE);
}


TestResult test_checker_dynamic_fixed_equality(void)
{
    return compile_fails(
        "var f: String<8> = \"x\"\nvar s: Text = \"x\"\nvar ok: Bool = f == s\n",
        "comparing fixed and dynamic strings",
        MESSAGE_CHECKER_OPERATOR_TYPE, "==");
}


TestResult test_checker_builder_capacity(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var b: TextBuilder = arena.builder(\"x\")\n",
        "builder with a non-Int32 capacity",
        MESSAGE_CHECKER_BUILDER_CAPACITY);
}


TestResult test_checker_builder_append_type(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var f: String<8> = \"x\"\n"
        "var b: TextBuilder = arena.builder(4)\n"
        "b.append(f)\n",
        "builder append with a fixed string",
        MESSAGE_CHECKER_BUILDER_APPEND_TYPE);
}


TestResult test_checker_builder_byte_type(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var b: TextBuilder = arena.builder(4)\n"
        "b.append_byte(\"x\")\n",
        "builder append_byte with a string",
        MESSAGE_CHECKER_BUILDER_BYTE_TYPE);
}


TestResult test_checker_builder_args(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var b: TextBuilder = arena.builder(4)\n"
        "var s: Text = b.finish(1)\n",
        "builder finish with an argument",
        MESSAGE_CHECKER_MEMBER_ARGS, "finish");
}


TestResult test_checker_builder_redefinition(void)
{
    return compile_fails(
        "type TextBuilder = Int32\n",
        "redefining TextBuilder",
        MESSAGE_CHECKER_REDEFINES_BUILDER);
}


TestResult test_checker_list_context(void)
{
    return compile_fails(
        "var xs: List<Int32> = [1]\n",
        "dynamic list literal",
        MESSAGE_CHECKER_LIST_LITERAL_CONTEXT);
}


TestResult test_checker_list_inferred_context(void)
{
    return compile_fails(
        "var xs = []\n",
        "empty inferred list literal",
        MESSAGE_CHECKER_LIST_INFER);
}


TestResult test_checker_list_length(void)
{
    return compile_fails(
        "var xs: Array<Int32, 2> = [1, 2, 3]\n",
        "fixed list over capacity",
        MESSAGE_CHECKER_LIST_LITERAL_LENGTH, 3, 2);
}


TestResult test_checker_list_element(void)
{
    return compile_fails(
        "var xs: Array<Store, 1> = []\n",
        "list element not sized",
        MESSAGE_CHECKER_ARRAY_ELEMENT);
}


TestResult test_checker_list_args(void)
{
    return compile_fails(
        "var xs: List = []\n",
        "bare List",
        MESSAGE_CHECKER_LIST_ARGS);
}


TestResult test_checker_list_capacity(void)
{
    return compile_fails(
        "var xs: Array<Int32, 4097> = []\n",
        "list capacity too large",
        MESSAGE_CHECKER_ARRAY_CAPACITY, NIPPY_DEFAULT_MAX_LIST_CAPACITY);
}


TestResult test_checker_list_index(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1]\nvar n: Int32 = xs[\"x\"]\n",
        "list index type",
        MESSAGE_CHECKER_LIST_INDEX);
}


TestResult test_checker_list_value(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list<Int32>(1)\n"
        "xs.push(true)\n",
        "list value type",
        MESSAGE_CHECKER_LIST_VALUE);
}


TestResult test_checker_list_create_type_arg(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list(1)\n",
        "list create type argument",
        MESSAGE_CHECKER_LIST_TYPE_ARG);
}


TestResult test_checker_list_create_args(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list<Int32>()\n",
        "list create argument count",
        MESSAGE_CHECKER_LIST_CREATE_ARGS);
}


TestResult test_checker_list_capacity_arg(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\n"
        "var xs: List<Int32> = arena.list<Int32>(\"x\")\n",
        "list capacity argument type",
        MESSAGE_CHECKER_LIST_CAPACITY_ARG);
}


TestResult test_checker_list_redefinition(void)
{
    return compile_fails(
        "type List = Int32\n",
        "redefining List",
        MESSAGE_CHECKER_REDEFINES_LIST);
}


TestResult test_checker_list_push_lvalue(void)
{
    return compile_fails(
        "global arena: Store = Store(16)\narena.list<Int32>(1).push(1)\n",
        "push temporary list",
        MESSAGE_CHECKER_LIST_LVALUE);
}


TestResult test_checker_list_index_type(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1]\nvar n: Int32 = xs[\"x\"]\n",
        "index type",
        MESSAGE_CHECKER_LIST_INDEX);
}


TestResult test_checker_slice_dynamic(void)
{
    return compile_fails(
        "var xs: Array<Int32, 1> = [1]\nvar s: Slice<Int32> = xs.slice(0, 1)\n",
        "slice fixed list",
        MESSAGE_CHECKER_SLICE_DYNAMIC);
}


TestResult test_checker_slice_readonly(void)
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


TestResult test_checker_slice_args(void)
{
    return compile_fails(
        "var s: Slice = []\n",
        "bare Slice",
        MESSAGE_CHECKER_SLICE_ARGS);
}


TestResult test_checker_slice_element(void)
{
    return compile_fails(
        "var s: Slice<Store> = nil\n",
        "Slice element type",
        MESSAGE_CHECKER_SLICE_ELEMENT);
}


TestResult test_checker_slice_redefinition(void)
{
    return compile_fails(
        "type Slice = Int32\n",
        "redefining Slice",
        MESSAGE_CHECKER_REDEFINES_SLICE);
}

const TestCase checker_test_2_tests[] = {
    {"store: global declaration", test_checker_global_declaration},
    {"store: global initializer", test_checker_global_store_initializer},
    {"store: duplicate name", test_checker_duplicate_store},
    {"store: parameter type", test_checker_store_parameter},
    {"store: return type", test_checker_store_return},
    {"store: struct field", test_checker_store_field},
    {"store: union alternative", test_checker_store_union},
    {"store: type arguments", test_checker_store_type_arguments},
    {"store: alloc type argument", test_checker_alloc_type_argument},
    {"store: alloc Store", test_checker_alloc_store},
    {"store: Handle argument count", test_checker_handle_args},
    {"store: alloc argument count", test_checker_alloc_count},
    {"store: alloc value type", test_checker_alloc_value_type},
    {"store: handle element", test_checker_handle_element},
    {"store: unknown store", test_checker_unknown_store},
    {"store: reset arguments", test_checker_reset_args},
    {"store: reset type argument", test_checker_reset_type_argument},
    {"store: read on non-handle", test_checker_read_on_non_handle},
    {"store: write type", test_checker_write_type},
    {"byte: literal range", test_checker_byte_range},
    {"store: unknown member", test_checker_unknown_member},
    {"string: dynamic to fixed conversion", test_checker_dynamic_string},
    {"string: concat with a fixed string", test_checker_concat_type},
    {"string: copy with a fixed string", test_checker_copy_type},
    {"string: store.text with a dynamic literal", test_checker_store_string_type},
    {"string: slice with a non-Int32 start", test_checker_slice_type},
    {"string: comparing fixed and dynamic strings",
     test_checker_dynamic_fixed_equality},
    {"builder: builder with a non-Int32 capacity", test_checker_builder_capacity},
    {"builder: builder append with a fixed string", test_checker_builder_append_type},
    {"builder: builder append_byte with a string", test_checker_builder_byte_type},
    {"builder: builder finish with an argument", test_checker_builder_args},
    {"builder: redefining TextBuilder", test_checker_builder_redefinition},
    {"list: dynamic list literal", test_checker_list_context},
    {"list: empty inferred literal", test_checker_list_inferred_context},
    {"list: fixed list over capacity", test_checker_list_length},
    {"list: list element not sized", test_checker_list_element},
    {"list: bare List", test_checker_list_args},
    {"list: list capacity too large", test_checker_list_capacity},
    {"list: push temporary list", test_checker_list_push_lvalue},
    {"list: index type", test_checker_list_index_type},
    {"slice: slice fixed list", test_checker_slice_dynamic},
    {"slice: slice index write", test_checker_slice_readonly},
    {"slice: bare Slice", test_checker_slice_args},
    {"slice: Slice element type", test_checker_slice_element},
    {"slice: redefining Slice", test_checker_slice_redefinition},
    {"list: list index type", test_checker_list_index},
    {"list: list value type", test_checker_list_value},
    {"list: list create type argument", test_checker_list_create_type_arg},
    {"list: list create argument count", test_checker_list_create_args},
    {"list: list capacity argument type", test_checker_list_capacity_arg},
    {"list: redefining List", test_checker_list_redefinition},
    {"string: a string literal larger than its capacity", test_checker_string_too_long},
    {"string: a string literal without a String<N> context",
     test_checker_string_literal_context},
    {"string: comparing two uncontextual string literals",
     test_checker_string_compare_context},
    {"string: a literal that fits two union alternatives",
     test_checker_string_ambiguous},
    {"string: a Text capacity above the limit", test_checker_string_capacity},
    {"string: assigning a String<8> to a String<16>", test_checker_string_assignment},
    {"string: comparing different Text capacities", test_checker_string_equality},
    {"string: byte_len with an argument", test_checker_string_len_args},
    {"string: byte_at with a string index", test_checker_string_index_type},
    {"string: a type argument on byte_len", test_checker_string_method_type_arg},
    {"string: byte_len on an Int32", test_checker_string_receiver},
    {"string: byte_len on an uncontextual literal",
     test_checker_string_literal_receiver},
    {"string: Error with too few arguments", test_checker_error_args},
    {"string: Error with a non-string description", test_checker_error_arg_type},
    {"string: Error with named arguments", test_checker_error_named_args},
    {"string: an Error field method on an Int32", test_checker_error_member},
    {"string: redefining String", test_checker_redefines_string},
    {"string: redefining Text", test_checker_redefines_text},
    {"string: redefining Error", test_checker_redefines_error},
    {"string: a variable named Text", test_checker_string_variable_name},
    {"assignment: an assignment to an unknown name", test_checker_assign_unknown},
    {"assignment: an assignment to a store", test_checker_assign_store},
    {"assignment: an assignment to a function", test_checker_assign_function},
    {"assignment: an assignment to a type", test_checker_assign_type},
    {"assignment: an assignment to a built-in type", test_checker_assign_builtin},
    {"assignment: assigning a program-level variable inside a function",
     test_checker_assign_global_in_function},
    {"assignment: assigning a Bool to an Int32", test_checker_assign_type_mismatch},
    {"assignment: assigning an out-of-range Byte literal",
     test_checker_assign_byte_range},
    {"assignment: assigning a literal larger than the destination",
     test_checker_assign_string_too_long},
};

const size_t checker_test_2_test_count =
    sizeof(checker_test_2_tests) / sizeof(checker_test_2_tests[0]);
