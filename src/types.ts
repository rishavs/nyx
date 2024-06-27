import type { ExprNode, RootNode } from "./ast";
import type { Token } from "./tokens";


export type CompilingError = {
    ok: false;
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

export type ExprParsingResult = {
    ok: boolean;
    result : ExprNode | CompilingError | null;
}