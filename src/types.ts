import type { ExprNode, RootNode } from "./ast";
import type { Token } from "./tokens";


export type CompilingError = {
    category: string;
    msg: string;
    start: number;
    end: number;
    line: number;
}

export type LexingContext = {
    src: string;
    i: number;
    line: number;
}

export type LexingResult = {
    tokens: Token[];
    errors: CompilingError[];
}

export type ParsingContext = {
    tokens: Token[];
    i: number;
}

export type ParsingResult = {
    root: RootNode;
    errors: CompilingError[];
}

// Expected expectedTokenKind for expectedSyntax, but instead found got
// Expected expectedSyntax, but instead found got
export type UnexpectedSyntax = {
    ok: false;
    expectedSyntax: string;
    expectedTokenKind?: string;
    got: Token;
}

export type ExprParsingResult = {
    ok: boolean;
    result : ExprNode | UnexpectedSyntax | null;
}