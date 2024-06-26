import { gen_c99, gen_root } from "./codegen";
import { expression } from "./expressions";
import { lex_file } from "./lexer";
import { parse_file } from "./parser";
import { root } from "./statements";
import type { ParsingContext } from "./types";

let src2 = `
    let z
    let x = 1
    let y = 2
    z = x + y
    
    return z`

let src = `exit (3, 7.88_7)
do_something (2, 5)`

let parsingStart = Date.now();

let lexingResult = lex_file(src);
let lexingEnd = Date.now();


console.log(`------------- Lexing : ${lexingEnd - parsingStart}ms ---------------`);
for (let token of lexingResult.tokens) {
    console.log(JSON.stringify(token));
}

if (lexingResult.errors.length > 0) {
    for (let error of lexingResult.errors) {
        console.error(error);
    }
    process.exit(1);
}

let p: ParsingContext = {
    tokens: lexingResult.tokens,
    i: 0,
}
let parsingResult = root(p);
// let parsingResult = expression(p);
let parsingEnd = Date.now();

console.log(`------------- Parsing : ${parsingEnd - lexingEnd}ms ---------------`);
console.log(JSON.stringify(parsingResult, null, 2));

if (!parsingResult.ok) {
    console.error(parsingResult);
    process.exit(1);
}
let code = gen_root(parsingResult);
console.log(`------------- Codegen : ${Date.now() - parsingEnd}ms ---------------`);    
console.log(code);
gen_c99(parsingResult)