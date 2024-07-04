import type { LexingContext, ParsingContext } from "./defs";

// --------------------------------------
// All Error Definitions
// --------------------------------------
export class SyntaxError extends Error {
    start: number;
    end: number;
    line: number;

    constructor(msg: string, start: number, end: number, line: number) {
        super(msg);
        this.name = "Syntax Error";
        this.start = start
        this.end = end
        this.line = line
    }
}

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

export class IllegalTokenError extends SyntaxError {
    constructor(l: LexingContext) {
        super(`Found : "${l.src[l.i]}", at ${l.line}:${l.i}`, l.i, l.i, l.line);
        this.name = this.name + ". Illegal Token";
        
        // Modify the stack trace to exclude the constructor call
        if (Error.captureStackTrace) {
            Error.captureStackTrace(this, IllegalTokenError);
        }
    }
}

export class UnclosedDelimiterError extends SyntaxError {
    constructor(l: LexingContext) {
        super(`Unclosed delimiter: "${l.src[l.i]}", at ${l.line}:${l.i}`, l.i, l.i, l.line);
        this.name = this.name + ". Unclosed Delimiter";
        
        // Modify the stack trace to exclude the constructor call
        if (Error.captureStackTrace) {
            Error.captureStackTrace(this, UnclosedDelimiterError);
        }
    }
}
// --------------------------------------
// Parser Errors
// --------------------------------------
export const raiseUnexpectedTokenError = (p: ParsingContext, expectedSyntax: string, expectedTokenKind?: string): TranspilingError =>  {
    return {
        ok: false,
        cat: 'SyntaxError',
        msg: `Expected ${expectedTokenKind ?
            expectedTokenKind + " for " :  ""}${expectedSyntax}, but instead found "${p.tokens[p.i].kind}"`,
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