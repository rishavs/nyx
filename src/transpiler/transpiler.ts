import { parseJsonText } from "typescript";
import type { LexingContext, ParsingContext } from "./defs";
import { lex_file } from "./lexer";
import { parse_file } from "./parser/parser";


export const transpile_file = (src: string): boolean => {
    let parsingStart = Date.now();
    let l: LexingContext = {
        src: src,
        i: 0,
        line: 0,
    };
    let lexingResult = lex_file(l);
    let lexingEnd = Date.now();
    let transpilingResult = false;

    if (lexingResult.errors.length > 0) {
        console.log(`Lexing Failed: ${lexingEnd - parsingStart}ms ---------------`);

        for (let error of lexingResult.errors) {
            console.error(error);
        }
        return false;
    }
    transpilingResult = true;
    console.log(`Lexing Done: ${lexingEnd - parsingStart}ms ---------------`);
    for (let token of lexingResult.tokens) {
        console.log(JSON.stringify(token));
    }

    let p: ParsingContext = {
        tokens: lexingResult.tokens,
        i: 0,
    }
    let parsingResult = parse_file(p);
    // let parsingResult = expression(p);
    let parsingEnd = Date.now();

    console.log(`------------- Parsing : ${parsingEnd - lexingEnd}ms ---------------`);
    console.log(JSON.stringify(parsingResult, null, 2));

    // if (!parsingResult.ok) {
    //     console.error(parsingResult);
    //     process.exit(1);
    // }
    // let code = gen_root(parsingResult);
    // console.log(`------------- Codegen : ${Date.now() - parsingEnd}ms ---------------`);    
    // console.log(code);
    // gen_c99(parsingResult)

    return transpilingResult;
}