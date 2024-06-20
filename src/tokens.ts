// export type TokenKind = 
//     // Special Tokens
//     'ILLEGAL' 
//     // Keywords
//     | 'let' | 'return'
//     // Delimited Tokens
//     | '(' | ')'
//     // Literals
//     | 'IDENTIFIER' | 'NUMBER'
//     // Unary Operators
//     | '!' | '-'
//     // Binary Operators
//     | '==' | '!=' | '<' | '<=' | '>' | '>=' | '+' | '-' | '*' | '/' | '&' | '|';


export const KwdTokenKind = {
    'let'       : null,
    'return'    : null,
}

export const OtherTokenKind = {
    'ILLEGAL'   : null,
    'NUMBER'    : null,
    'IDENTIFIER': null,
}

export const OprTokenKind = {
    // unary 
    '!' : null,

    // binary
    '==' : null,
    '!=' : null,
    '<'  : null,
    '<=' : null,
    '>'  : null,
    '>=' : null,
    '+'  : null,
    '-'  : null,
    '*'  : null,
    '/'  : null,
    '&'  : null,
    '|'  : null,

    // delimiters
    '(' : null,
    ')' : null,

    // Special
    '=' : null,
}

export type TokenKind = 
    keyof typeof KwdTokenKind | keyof typeof OprTokenKind | keyof typeof OtherTokenKind;

export type Token = {
    kind: TokenKind;
    value: string;
    i: number;
    line: number;
}