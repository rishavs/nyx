import type { RootNode } from "./ast";
import type { TranspilingError } from "./errors";
import type { Token } from "./tokens";

export type LexingContext = {
    src: string;
    i: number;
    line: number;
}
export type LexingResult = {
    ok: boolean;
    tokens: Token[];
    errors: TranspilingError[];
}

export type ParsingContext = {
    tokens: Token[];
    i: number;
}

export type ParsingResult = {
    ok: boolean;
    root: RootNode | null;
    errors: TranspilingError[];
}

export type TranspilingContext = {
    file: string;

    src: string;
    i: number;
    line: number;
    lexingDuration: number;

    tokens: Token[];
    j: number
    parsingDuration: number;

    root?: RootNode;

    transpilingDuration: number;
    errors: TranspilingError[];
}