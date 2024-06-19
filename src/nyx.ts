import { lex_file } from "./lexer";
import { type LexingContext, type RootNode, StmtNodeKind, type ParsingContext } from "./types";


let l: LexingContext = {
    src: `
    let z
    let x = 1
    let y = 2
    z = x + y
    
    return z`,
    i: 0,
    line: 0,
}

let lexingResult = lex_file(l);
// print tokens as flat list
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
let parsingResult = parse_file(p);