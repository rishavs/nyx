/* Checker rules for user-defined associated functions. */

#include "compile_support.h"

TEST("method owner must be a concrete struct")
{
    return compile_fails(
        "method Missing.move()\n  return\nend\n",
        "a method whose owner type is missing",
        MESSAGE_CHECKER_METHOD_OWNER, "Missing");
}

TEST("generic method on concrete struct")
{
    return compile_accepts(
        "type Point x: Int32 end\n"
        "method Point.convert<T>(value: T): T\n"
        "  return value\n"
        "end\n"
        "var p = Point(1)\n"
        "var n = p.convert(3)\n",
        "a generic method on a concrete struct");
}

TEST("generic associated function requires inferable type")
{
    return compile_fails(
        "type Point x: Int32 end\n"
        "static Point::make<T>(): T\n  return nil\nend\n"
        "var n: Int32 = Point::make()\n",
        "a generic static function with an uninferable type parameter",
        MESSAGE_CHECKER_CANNOT_INFER_TYPE, "T", "make");
}

TEST("generic string argument requires type context")
{
    return compile_fails(
        "type Point x: Int32 end\n"
        "static Point::identity<T>(x: T): T\n  return x\nend\n"
        "var value = Point::identity(\"hello\")\n",
        "a generic string argument without a concrete type",
        MESSAGE_CHECKER_CANNOT_INFER_TYPE, "T", "identity");
}

TEST("contextual generic string checks capacity")
{
    return compile_fails(
        "type Point x: Int32 end\n"
        "static Point::identity<T>(x: T): T\n  return x\nend\n"
        "var value: String<4> = Point::identity(\"hello\")\n",
        "a contextual generic string exceeding its capacity",
        MESSAGE_CHECKER_STRING_TOO_LONG, (size_t)5, 4);
}

TEST("generic method requires a mutable receiver")
{
    return compile_fails(
        "type Point x: Int32 end\n"
        "method Point.choose<T>(value: T): T\n  return value\nend\n"
        "const p = Point(1)\n"
        "var n = p.choose(2)\n",
        "a generic method called on a const receiver",
        MESSAGE_CHECKER_CONST_ASSIGN, "p");
}

TEST("generic owner requires its type parameters")
{
    return compile_fails(
        "type Box<T> value: T end\n"
        "method Box.read(): T\n"
        "  return self.value\n"
        "end\n",
        "a method whose owner type is generic",
        MESSAGE_CHECKER_METHOD_OWNER_PARAMETERS, "Box");
}

TEST("generic owner parameters must match declaration")
{
    return compile_fails(
        "type Box<T> value: T end\n"
        "method Box<U>.read(): U\n  return self.value\nend\n",
        "an associated function with mismatched owner parameters",
        MESSAGE_CHECKER_METHOD_OWNER_PARAMETERS, "Box");
}

TEST("generic static owner requires explicit type arguments")
{
    return compile_fails(
        "type Box<T> value: T end\n"
        "static Box<T>::make(value: T): Box<T>\n  return Box(value)\nend\n"
        "var b = Box::make(1)\n",
        "a generic static call without owner type arguments",
        MESSAGE_CHECKER_MISSING_TYPE_ARGUMENTS, "Box");
}

TEST("associated type parameter cannot shadow owner parameter")
{
    return compile_fails(
        "type Box<T> value: T end\n"
        "method Box<T>.choose<T>(value: T): T\n  return value\nend\n",
        "a function type parameter duplicating an owner parameter",
        MESSAGE_CHECKER_DUPLICATE_TYPE_PARAMETER, "T");
}

TEST("associated type parameters respect combined limit")
{
    return compile_fails(
        "type Box<A,B,C,D,E,F,G,H> value: A end\n"
        "method Box<A,B,C,D,E,F,G,H>.choose<I>(x: I): I\n"
        "  return x\nend\n",
        "a method exceeding the combined type parameter limit",
        MESSAGE_CHECKER_TOO_MANY_TYPE_PARAMETERS, "choose", (size_t)8);
}

TEST("generic static owner rejects wrong argument type")
{
    return compile_fails(
        "type Box<T> value: T end\n"
        "static Box<T>::make(value: T): Box<T>\n  return Box(value)\nend\n"
        "var b = Box<Int32>::make(true)\n",
        "an argument incompatible with a specialized owner",
        MESSAGE_CHECKER_FUNCTION_ARG_TYPE, (size_t)1, "make");
}

TEST("non-generic owner rejects static type arguments")
{
    return compile_fails(
        "type Point x: Int32 end\n"
        "static Point::read(p: Point): Int32\n  return p.x\nend\n"
        "var n = Point<Int32>::read(Point(1))\n",
        "type arguments on a non-generic static owner",
        MESSAGE_CHECKER_TYPE_NOT_GENERIC, "Point");
}

TEST("duplicate associated function")
{
    return compile_fails(
        "type Point x: Int32 end\n"
        "method Point.move()\n  return\nend\n"
        "static Point::move()\n  return\nend\n",
        "a duplicate instance or static function name on one type",
        MESSAGE_CHECKER_DUPLICATE_METHOD, "Point", "move");
}

TEST("method requires mutable lvalue receiver")
{
    return compile_fails(
        "type Point x: Int32 end\n"
        "method Point.move(): self\n  return self\nend\n"
        "var p = Point(1).move()\n",
        "an instance method call on a temporary value",
        MESSAGE_CHECKER_METHOD_RECEIVER, "move");
}

TEST("method cannot mutate a const receiver")
{
    return compile_fails(
        "type Point x: Int32 end\n"
        "method Point.move(): self\n"
        "  self.x = self.x + 1\n"
        "  return self\n"
        "end\n"
        "const p = Point(1)\n"
        "var q = p.move()\n",
        "an instance method call on a const struct",
        MESSAGE_CHECKER_CONST_ASSIGN, "p");
}

TEST("method cannot mutate a const narrowed union receiver")
{
    return compile_fails(
        "type Point x: Int32 end\n"
        "method Point.move(): self\n"
        "  self.x = self.x + 1\n"
        "  return self\n"
        "end\n"
        "const p: Point | Nil = Point(1)\n"
        "if typeof p is Point then\n"
        "  p.move()\n"
        "end\n",
        "an instance method call on a const narrowed union",
        MESSAGE_CHECKER_CONST_ASSIGN, "p");
}

TEST("implicit receiver cannot be reassigned")
{
    return compile_fails(
        "type Point x: Int32 end\n"
        "method Point.move(): self\n"
        "  self = self\n"
        "  return self\n"
        "end\n",
        "assignment to the implicit receiver",
        MESSAGE_CHECKER_SELF_ASSIGN);
}

TEST("unknown static function")
{
    return compile_fails(
        "type Point x: Int32 end\n"
        "var p = Point::missing()\n",
        "a call to an unknown static function",
        MESSAGE_CHECKER_UNKNOWN_CALL, "Point::missing");
}
