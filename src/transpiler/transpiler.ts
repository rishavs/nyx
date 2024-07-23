import { gen_c99, gen_root } from "./codegen/generator";
import type { LexingContext, LexingResult, ParsingContext } from "./defs";
import { TranspilingError } from "./errors";
import { lex_file } from "./lexer";
import { parse_file } from "./parser/parser";
import type { Token } from "./tokens";


export const transpile_file = async (src: string) => {
    // On any error in a stage, recover and document all errors for that stage
    // All future stages should be skipped
    let transpilingResult = false;
    // --------------------------------------
    // Lexing
    // --------------------------------------
    let lexingStart = Date.now();
    let l: LexingContext = {
        src: src,
        i: 0,
        line: 0,
    };
    let lexingResult: Token[] | TranspilingError[] = lex_file(l);
    let lexingDuration = Date.now() - lexingStart;
    if (lexingResult[0] && lexingResult[0] instanceof TranspilingError) {
        console.log(`Lexing Failed: ${lexingDuration}ms ---------------`);
        console.log("Number of errors: ", lexingResult.length)

        for (let error of lexingResult) {
            console.error(error);
        }
        return false;
    }
    transpilingResult = true;
    console.log(`Lexing Done: ${lexingDuration}ms ---------------`);
    for (let token of lexingResult) {
        console.log(JSON.stringify(token));
    }

    // --------------------------------------
    // Parsing
    // --------------------------------------
    let p: ParsingContext = {
        tokens: lexingResult as Token[],
        i: 0,
    }
    let parsingStart = Date.now();
    let parsingResult = parse_file(p);
    let parsingDuration = Date.now() - parsingStart;

    console.log(`------------- Parsing : ${parsingDuration}ms ---------------`);
    console.log(JSON.stringify(parsingResult, null, 2));

    if (Array.isArray(parsingResult)) {
        console.error(parsingResult);
        process.exit(1);
    }
    // let code = gen_root(parsingResult);
    // console.log(`------------- Codegen : ${Date.now() - parsingEnd}ms ---------------`);    
    // console.log("Generated: ", code);
    // await gen_c99(parsingResult)

    return transpilingResult;
}