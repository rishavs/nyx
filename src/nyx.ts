import { lex_file } from "./lexer";
import { parse_expression } from "./expressions";
import type { ParsingContext } from "./types";

let src2 = `
    let z
    let x = 1
    let y = 2
    z = x + y
    
    return z`

let src = `1 + 3`

let lexingStart = Date.now();

let lexingResult = lex_file(src2);
// print time taken
console.log(`Lexing took ${Date.now() - lexingStart}ms`);

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
let parsingResult = parse_expression(p);
console.log('------------------- Parsing Result -------------------');
console.log(parsingResult);