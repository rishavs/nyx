
export const KwdTokenKind = [
    'let',
    'return',
]

export const OtherTokenKind = [
    'ILLEGAL',
    'NUMBER',
    'IDENTIFIER',
]

export const OprTokenKind = [
    // All 2-char operators
    '==', '!=', '>=', '<=', '&&', '||', '++', '--', '**', 
    '+=', '-=', '*=', '/=', '%=', '<<', '>>', '&=', '|=', '^=', '=>', 

    // All 1-char operators
    '+', '-', '*', '/', '%', '>', '<', '!', '=', '&', '|', 
    '^', '~', '?', ':', ';', ',', '.', '(', ')', '[', ']', '{', '}',

]

export type TokenKind = 
    typeof KwdTokenKind[number] | typeof OprTokenKind[number] | typeof OtherTokenKind[number];

export type Token = {
    kind: TokenKind;
    value: string;
    start: number;
    end: number;
    line: number;
}