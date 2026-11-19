/* Sum types declared with cases: `type Shape is | Circle is radius: Int32 end |
   Empty end`. What the parser accepts, what the checker rejects, and the
   built-in ErrorKind an Error carries. */

#include "compile_support.h"

TEST("a sum type of cases with and without payloads")
{
    return compile_accepts(
        "type Shape is\n"
        "  | Circle is radius: Int32 end\n"
        "  | Square is side: Int32, label: String<8> end\n"
        "  | Empty\n"
        "end\n"
        "var a: Shape = Shape::Circle(radius = 3)\n"
        "var b: Shape = Shape::Square(4, \"box\")\n"
        "var c = Shape::Empty()\n",
        "a sum type with payload and unit cases");
}

TEST("a pipe may lead or trail the cases")
{
    TestResult leading = compile_accepts(
        "type A is | X | Y end\nvar a: A = A::X()\n", "a leading pipe");
    TestResult trailing = compile_accepts(
        "type B is X | Y | end\nvar b: B = B::Y()\n", "a trailing pipe");
    TestResult bare = compile_accepts(
        "type C is X | Y end\nvar c: C = C::Y()\n", "no pipe at all");

    return leading != TEST_PASS ? leading : trailing != TEST_PASS ? trailing
                                                                  : bare;
}

TEST("a sum type with no cases is rejected")
{
    return compile_fails("type S is end\n", "an empty sum type",
                         MESSAGE_PARSER_EXPECTED_CASE_NAME);
}

TEST("a case payload needs a field")
{
    return compile_fails("type S is | A is end | B end\n", "an empty payload",
                         MESSAGE_PARSER_EXPECTED_AT_LEAST_ONE_FIELD);
}

TEST("cases are separated by pipes")
{
    return compile_fails("type S is | A B end\n", "two cases without a pipe",
                         MESSAGE_PARSER_EXPECTED_CASE_OR_END);
}

TEST("a case name may not repeat")
{
    return compile_fails("type S is | A | B | A end\n", "a repeated case",
                         MESSAGE_CHECKER_DUPLICATE_CASE, "A", "S");
}

TEST("a case may not repeat a field")
{
    return compile_fails("type S is | A is x: Int32, x: Int32 end | B end\n",
                         "a repeated field", MESSAGE_CHECKER_DUPLICATE_FIELD,
                         "x", "A");
}

TEST("a case built by a name the type does not have")
{
    return compile_fails("type S is | A | B end\nvar s: S = S::C()\n",
                         "an unknown case", MESSAGE_CHECKER_UNKNOWN_CASE, "S",
                         "C");
}

TEST("a case written as a type must exist")
{
    TestResult unknown_case = compile_fails(
        "type S is | A | B end\nfun f(s: S): Bool\n  return typeof s is S::C\nend\n",
        "an unknown case type", MESSAGE_CHECKER_UNKNOWN_CASE, "S", "C");
    TestResult not_sum = compile_fails(
        "type P x: Int32 end\nfun f(p: P): Bool\n  return typeof p is P::C\nend\n",
        "a case of a struct", MESSAGE_CHECKER_NOT_A_SUM_TYPE, "P");

    if (unknown_case != TEST_PASS) return unknown_case;
    return not_sum;
}

TEST("a sum type is not a constructor")
{
    return compile_fails("type S is | A | B end\nvar s = S()\n",
                         "calling the sum type", MESSAGE_CHECKER_CONSTRUCTOR_NOT_STRUCT,
                         "S");
}

TEST("a match over a sum type must cover every case")
{
    TestResult partial = compile_fails(
        "type S is | A | B | C end\n"
        "fun f(s: S): Int32\n"
        "  match typeof s\n"
        "  with S::A then\n    return 1\n"
        "  with S::B then\n    return 2\n"
        "  end\n"
        "  return 0\n"
        "end\n",
        "a match that misses a case", MESSAGE_CHECKER_MATCH_NOT_EXHAUSTIVE);
    TestResult full = compile_accepts(
        "type S is | A | B | C end\n"
        "fun f(s: S): Int32\n"
        "  match typeof s\n"
        "  with S::A then\n    return 1\n"
        "  with S::B then\n    return 2\n"
        "  with S::C then\n    return 3\n"
        "  end\n"
        "end\n",
        "a match that covers every case");

    return partial != TEST_PASS ? partial : full;
}

TEST("a payload is readable once the case is known")
{
    TestResult known = compile_accepts(
        "type S is | A is n: Int32 end | B end\n"
        "fun f(s: S): Int32\n"
        "  if typeof s is S::A then\n    return s.n\n  end\n"
        "  return 0\nend\n",
        "a payload read after a type test");
    TestResult unknown = compile_fails(
        "type S is | A is n: Int32 end | B end\n"
        "fun f(s: S): Int32\n  return s.n\nend\n",
        "a payload read before a type test", MESSAGE_CHECKER_UNKNOWN_FIELD, "n");

    return known != TEST_PASS ? known : unknown;
}

TEST("a generic sum type")
{
    return compile_accepts(
        "type Maybe<T> is | Some is value: T end | None end\n"
        "fun make(n: Int32): Maybe<Int32>\n"
        "  if n > 0 then\n    return Maybe::Some(value = n)\n  end\n"
        "  return Maybe::None()\n"
        "end\n"
        "fun get(m: Maybe<Int32>): Int32\n"
        "  if typeof m is Maybe<Int32>::Some then\n    return m.value\n  end\n"
        "  return 0\n"
        "end\n"
        "var x: Maybe<String<4>> = Maybe<String<4>>::Some(value = \"hi\")\n",
        "a generic sum type built, tested and read");
}

TEST("a generic sum type needs its type arguments")
{
    return compile_fails(
        "type Maybe<T> is | Some is value: T end | None end\n"
        "fun f(m: Maybe<Int32>): Bool\n  return typeof m is Maybe::Some\nend\n",
        "a generic case without arguments", MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS,
        "Maybe");
}

TEST("sum type values compare with ==")
{
    TestResult same = compile_accepts(
        "type D is | N | S end\n"
        "var a: D = D::N()\nvar b: D = D::S()\n"
        "var same: Bool = a == b\n",
        "equal sum type values");
    TestResult payload = compile_accepts(
        "type R is | Ok is v: Int32 end | Err is c: Int32 end end\n"
        "var a: R = R::Ok(1)\nvar same: Bool = a == R::Ok(1)\n",
        "sum type values with payloads");

    return same != TEST_PASS ? same : payload;
}

TEST("an Error carries an ErrorKind")
{
    return compile_accepts(
        "var e: Error = Error(ErrorKind::NotFound(), \"missing\", \"f.nip\", 1, 2, 3)\n"
        "var k: ErrorKind = e.kind\n"
        "var other: ErrorKind = ErrorKind::Other(header = \"MINE\")\n"
        "var t: Error = Error::throw(other, \"custom\")\n"
        "var h: String<32> = t.header()\n"
        "var line: Int32 = e.line\n",
        "an Error built from kinds");
}

TEST("an Error's kind is not a string")
{
    return compile_fails("var e: Error = Error(\"E\", \"d\", \"f\", 1, 2, 3)\n",
                         "a string where the kind goes",
                         MESSAGE_CHECKER_ERROR_ARG_TYPE, (size_t)1);
}

TEST("ErrorKind is built in and cannot be redefined")
{
    return compile_fails("type ErrorKind is | A | B end\n",
                         "redefining ErrorKind",
                         MESSAGE_CHECKER_REDEFINES_ERROR_KIND);
}

TEST("a program that never names Error has no ErrorKind")
{
    CompilerResult plain = compiler_compile("var n: Int32 = 1\n", &test_config);
    CompilerResult named = compiler_compile(
        "var k: ErrorKind = ErrorKind::Closed()\n", &test_config);
    int ok = plain.succeeded && plain.h_source != NULL &&
             strstr(plain.h_source, "nyx_sum_") == NULL &&
             named.succeeded && named.h_source != NULL &&
             strstr(named.h_source, "nyx_sum_") != NULL;

    compiler_result_free(&plain);
    compiler_result_free(&named);
    if (!ok) {
        fprintf(stderr, "  ErrorKind was not added exactly when named\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

TEST("a case written as a type reads back in a diagnostic")
{
    TestResult plain = compile_fails(
        "type S is | A is n: Int32 end | B end\nvar x: S::A = 5\n",
        "a case type in a mismatch", MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH,
        "x", "S::A");
    TestResult generic = compile_fails(
        "type M<T> is | Some is v: T end | None end\n"
        "var x: M<Int32>::Some = 5\n",
        "a generic case type in a mismatch",
        MESSAGE_CHECKER_INITIALISER_TYPE_MISMATCH, "x", "M<Int32>::Some");

    return plain != TEST_PASS ? plain : generic;
}
