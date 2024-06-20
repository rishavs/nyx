import type { Token } from "./tokens";
import { SpecialTokenKind, OprTokenKind, KwdTokenKind } from "./tokens";
import type { LexingResult, LexingContext, CompilingError } from "./types";

// Character definitions
const isAlphabet = (c: string): boolean =>  
    'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ'.includes(c);

const isDigit = (c: string): boolean => 
    '0123456789'.includes(c);

export const lex_file = (src: string): LexingResult => {
    let l: LexingContext = {
        src: src,
        i: 0,
        line: 0,
    };
    let result : LexingResult= {
        tokens: [],
        errors: []
    };

    while (l.i < l.src.length) {
        let c = l.src[l.i];
        let token: Token;

        switch (true) {
            case ' \n\t'.includes(c):
                ws(l);
                break;

            case isAlphabet(c) || c === '_':
                token = kwdOrId(l);
                result.tokens.push(token);
                break;

            case isDigit(c):
                token = number(l);
                result.tokens.push(token);
                break;

            default:
                token = oprOrIllegal(l);
                result.tokens.push(token);
                if (token.kind === SpecialTokenKind.ILLEGAL) {
                    let error: CompilingError = {
                        category: 'Lexing',
                        msg: `Illegal token ${token.value}`,
                        i: token.i,
                        line: token.line
                    };
                    result.errors.push(error);
                }
                break;
        }
    }
    
    return result;
}

const oprOrIllegal = (l: LexingContext): Token => {
    let cursor = l.i;
    while (cursor < l.src.length && 
        !(
            isAlphabet(l.src[cursor]) || 
            isDigit( l.src[cursor] ) || 
            ' \n\t'.includes(l.src[cursor]) || 
            l.src[cursor] === '_'
        )
    ){
        cursor++;
    }
    let value = l.src.slice(l.i, cursor);

    // iterate over the FixedTokenKind enum
    for (let key of Object.keys(OprTokenKind)) {
        if (value === OprTokenKind[key as keyof typeof OprTokenKind]) {
            let token = {
                kind: OprTokenKind[key as keyof typeof OprTokenKind],
                value: value,
                i: l.i,
                line: l.line
            };
            l.i = cursor;
            return token;
        }
    }
    
    let token = {
        kind: SpecialTokenKind.ILLEGAL,
        value: value,
        i: l.i,
        line: l.line
    };
    l.i = cursor;
    return token;
}

const number = (l: LexingContext): Token => {
    let cursor = l.i;
    let isFloat = false;
    let value = '';
    while (
        cursor < l.src.length && 
        (isDigit(l.src[cursor]) || l.src[cursor] === '.' || l.src[cursor] === '_' )
    ) {
        if (l.src[cursor] === '_') {
            cursor++;
            continue;
        }
        if (l.src[cursor] === '.') {
            if (isFloat) {
                break;
            }
            isFloat = true;
        }
        value += l.src[cursor];
        cursor++;
    }
    let token = {
        kind: SpecialTokenKind.NUMBER,
        value: value,
        i: l.i,
        line: l.line
    };
    l.i = cursor;
    return token;
}


const ws = (l: LexingContext) => {
    let cursor = l.i;
    while (cursor < l.src.length && ' \n\t'.includes(l.src[cursor]) ){
        if (l.src[cursor] === '\n') {
            l.line++;
        }
        cursor++;
    }
    l.i = cursor;
    // return {
    //     kind: SpecialTokenKind.WS,
    //     value: '',
    //     i: l.i,
    //     line: l.line
    // };
}

const kwdOrId = (l: LexingContext): Token => {
    let cursor = l.i;
    while (
        cursor < l.src.length && 
        (isAlphabet(l.src[cursor]) || isDigit(l.src[cursor]) || l.src[cursor] === '_' )
    ) {
        cursor++;
    }
    let value = l.src.slice(l.i, cursor);

    // iterate over the FixedTokenKind enum
    for (let key of Object.keys(KwdTokenKind)) {
        if (value === KwdTokenKind[key as keyof typeof KwdTokenKind]) {
            let token = {
                kind: KwdTokenKind[key as keyof typeof KwdTokenKind],
                value: value,
                i: l.i,
                line: l.line
            };
            l.i = cursor;
            return token;
        }
    }
    
    let token = {
        kind: SpecialTokenKind.IDENTIFIER,
        value: value,
        i: l.i,
        line: l.line
    };
    l.i = cursor;
    return token;
}
