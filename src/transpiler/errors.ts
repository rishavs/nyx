import type { LexingContext, ParsingContext } from "./defs";

// --------------------------------------
// All Error Definitions
// --------------------------------------
export type TranspilingError = {
    ok: false;
    cat: string;
    msg: string;
    start: number;
    end: number;
    line: number;
}
// --------------------------------------
// Lexer Errors
// --------------------------------------
export const raiseIllegalTokenError = (l: LexingContext): TranspilingError => {
    return {
        ok: false,
        cat: 'SyntaxError',
        msg: `Found Illegal token: "${l.src[l.i]}"`,
        start: l.i,
        end: l.i,
        line: l.line
    }
}

export const raiseUnclosedDelimiterError = (l: LexingContext): TranspilingError => {
    return {
        ok: false,
        cat: 'SyntaxError',
        msg: `Unclosed delimiter: "${l.src[l.i]}"`,
        start: l.i,
        end: l.i,
        line: l.line
    }
}

// --------------------------------------
// Parser Errors
// --------------------------------------
export const raiseUnexpectedTokenError = (p: ParsingContext, expectedSyntax: string, expectedTokenKind?: string): TranspilingError =>  {
    return {
        ok: false,
        cat: 'SyntaxError',
        msg: `Expected ${expectedTokenKind}${
            expectedTokenKind ? " for " :  ""}${expectedSyntax}, but instead found ${p.tokens[p.i].kind}`,
        start: p.tokens[p.i].start,
        end: p.tokens[p.i].end,
        line: p.tokens[p.i].line
    } as TranspilingError
}

export const raiseUnexpectedEndOfInput = (p: ParsingContext, expectedSyntax: string): TranspilingError => {
    return {
        ok: false,
        cat: 'SyntaxError',
        msg: `Expected ${expectedSyntax}, but instead found end of input`,
        start: p.tokens[p.i].start,
        end: p.tokens[p.i].end,
        line: p.tokens[p.i].line
    } as TranspilingError
}