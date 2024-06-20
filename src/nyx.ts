import { expression } from "./expressions";
import { lex_file } from "./lexer";
import type { ParsingContext } from "./types";

let src2 = `
    let z
    let x = 1
    let y = 2
    z = x + y
    
    return z`

let src = `66 != 67 > !2`

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
let parsingResult = expression(p);
let parsingEnd = Date.now();

console.log(`------------- Parsing : ${parsingEnd - lexingEnd}ms ---------------`);
console.log(JSON.stringify(parsingResult, null, 2));
