// Nil	nil
// Bool	true, false
// Integers	18, -12, 19_i64, 14_u32,64_u8
// Floats	1.0, 1.0_f32, 1e10, -0.5
// Char	'a', '\n', 'あ'
// String	"foo\tbar", %("あ"), %q(foo #{foo})
// Symbol	:symbol, :"foo bar"
// Array	[1, 2, 3], [1, 2, 3] of Int32, %w(one two three)
// Array-like	Set{1, 2, 3}
// Hash	{"foo" => 2}, {} of String => Int32
// Hash-like	MyType{"foo" => "bar"}
// Range	1..9, 1...10, 0..var
// Regex	/(foo)?bar/, /foo #{foo}/imx, %r(foo/)
// Tuple	{1, "hello", 'x'}
// NamedTuple	{name: "Crystal", year: 2011}, {"this is a key": 1}
// Proc	->(x : Int32, y : Int32) { x + y }
// Command	`echo foo`, %x(echo foo)
// Previous