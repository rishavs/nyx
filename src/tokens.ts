export enum SpecialTokenKind {
    ILLEGAL = 'ILLEGAL',

    NUMBER = 'NUMBER',
    IDENTIFIER = 'IDENTIFIER',
    LPAREN = '(',
    RPAREN = ')',
}

// These tokens have a fixed string value
export enum KwdTokenKind {
    LET = 'let',
    RETURN = 'return',
}

export enum OprTokenKind {
    NOT = '!',
    NEGATE = '-',
    PLUS = '+',
    EQUALS = '==',
    ASSIGN = '=',
}
export type TokenKind = SpecialTokenKind | KwdTokenKind | OprTokenKind;

export type Token = {
    kind: TokenKind;
    value: string;
    i: number;
    line: number;
}

