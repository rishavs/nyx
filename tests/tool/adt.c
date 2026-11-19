/* Sum types in generated programs: what they build, test, match, compare and
   print, and the ErrorKind an Error carries. */

#include "tool_support.h"

TEST("a sum type is built, tested and read")
{
    return run_generated(
        "type Shape is\n"
        "  | Circle is radius: Int32 end\n"
        "  | Square is side: Int32 end\n"
        "  | Empty\n"
        "end\n"
        "fun area(s: Shape): Int32\n"
        "  match typeof s\n"
        "  with Shape::Circle then\n    return s.radius * s.radius * 3\n"
        "  with Shape::Square then\n    return s.side * s.side\n"
        "  with Shape::Empty then\n    return 0\n"
        "  end\n"
        "end\n"
        "var a: Shape = Shape::Circle(radius = 3)\n"
        "var b = Shape::Square(side = 4)\n"
        "var c = Shape::Empty()\n"
        "print(area(a), area(b), area(c))\n",
        "27 16 0\n", "sum type areas");
}

TEST("a sum type of unit cases is a choice")
{
    return run_generated(
        "type Dir is | North | East | South | West end\n"
        "fun turn(d: Dir): Dir\n"
        "  return match typeof d\n"
        "    with Dir::North then Dir::East()\n"
        "    with Dir::East then Dir::South()\n"
        "    with Dir::South then Dir::West()\n"
        "    else Dir::North()\n"
        "  end\n"
        "end\n"
        "var d: Dir = Dir::North()\n"
        "d = turn(turn(d))\n"
        "print(d)\n"
        "print(d == Dir::South(), d == Dir::West())\n",
        "South\ntrue false\n", "unit cases");
}

TEST("a sum type prints its case")
{
    return run_generated(
        "type R is | Ok is value: Int32 end | Err is code: Int32, note: String<8> end end\n"
        "var a: R = R::Ok(1)\n"
        "var b: R = R::Err(7, \"bad\")\n"
        "print(a)\n"
        "print(b)\n",
        "Ok { value: 1 }\nErr { code: 7, note: bad }\n", "printing cases");
}

TEST("a generic sum type")
{
    return run_generated(
        "type Maybe<T> is | Some is value: T end | None end\n"
        "fun make(n: Int32): Maybe<Int32>\n"
        "  if n > 0 then\n    return Maybe::Some(value = n)\n  end\n"
        "  return Maybe::None()\n"
        "end\n"
        "fun get(m: Maybe<Int32>): Int32\n"
        "  if typeof m is Maybe<Int32>::Some then\n    return m.value\n  end\n"
        "  return -1\n"
        "end\n"
        "print(get(make(7)), get(make(0)))\n"
        "var flag: Maybe<Bool> = Maybe::Some(value = true)\n"
        "print(flag)\n"
        "var none: Maybe<Bool> = Maybe::None()\n"
        "print(none)\n",
        "7 -1\nSome { value: true }\nNone\n", "generic sum type");
}

TEST("sum type values with payloads compare by case and value")
{
    return run_generated(
        "type R is | Ok is v: Int32 end | Err is c: Int32 end end\n"
        "var a: R = R::Ok(1)\n"
        "print(a == R::Ok(1), a == R::Ok(2), a == R::Err(1))\n",
        "true false false\n", "sum type equality");
}

TEST("a sum type in a struct, an array and a list")
{
    return run_generated(
        "global arena: Store = Store(1024)\n"
        "type Light is | Red | Green end\n"
        "type Lamp\n  light: Light\n  id: Int32\nend\n"
        "var lamp = Lamp(Light::Green(), 5)\n"
        "var all: Array<Light, 3> = [Light::Red(), Light::Green(), Light::Red()]\n"
        "var list: List<Light> = arena.list<Light>(4)\n"
        "list.push(Light::Green())\n"
        "print(lamp.light, lamp.id, all[1], list.len())\n",
        "Green 5 Green 1\n", "sum types as members");
}

TEST("an ErrorKind is matched, compared and read as a header")
{
    return run_generated(
        "fun describe(e: Error): Int32\n"
        "  return match typeof e.kind\n"
        "    with ErrorKind::TimedOut then 1\n"
        "    with ErrorKind::NotFound then 2\n"
        "    else 0\n"
        "  end\n"
        "end\n"
        "var a: Error = Error(ErrorKind::TimedOut(), \"slow\", \"f.nip\", 1, 2, 3)\n"
        "var b: Error = Error::throw(ErrorKind::NotFound(), \"missing\")\n"
        "var c: Error = Error::throw(ErrorKind::Other(header = \"E_MINE\"), \"custom\")\n"
        "print(describe(a), describe(b), describe(c))\n"
        "print(a.kind == ErrorKind::TimedOut(), a.kind == b.kind)\n"
        "print(a.header(), b.header(), c.header())\n"
        "print(a.line(), a.col(), a.pos(), a.line)\n",
        "1 2 0\ntrue false\nTimedOut NotFound E_MINE\n1 2 3 1\n",
        "error kinds");
}

TEST("an ErrorKind in a union with the value that failed")
{
    return run_generated(
        "fun find(n: Int32): Int32 | Error\n"
        "  if n > 0 then\n    return n\n  end\n"
        "  return Error::throw(ErrorKind::InvalidInput(), \"not positive\")\n"
        "end\n"
        "fun code(r: Int32 | Error): Int32\n"
        "  if typeof r is Int32 then\n    return r\n  end\n"
        "  if typeof r is Error then\n"
        "    if typeof r.kind is ErrorKind::InvalidInput then\n      return -1\n    end\n"
        "  end\n"
        "  return -2\n"
        "end\n"
        "print(code(find(5)), code(find(0)))\n",
        "5 -1\n", "an Error result");
}
