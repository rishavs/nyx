import { KwdTokenKind, OprTokenKind, type Token } from "./tokens";
import type { LexingResult, LexingContext, CompilingError } from "./types";

// Character definitions
// TODO - lexer broken for consequtive operators

const isAlphabet = (c: string): boolean =>  
    'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ'.includes(c);

const isDigit = (c: string): boolean => 
    '0123456789'.includes(c);

const isSpecialChar = (c: string): boolean =>
    !isAlphabet(c) && !isDigit(c) && !' \n\t'.includes(c) && c !== '_';

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
        let token: Token | null;

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

            case isSpecialChar(c):
                token = opr(l)
                if (token) result.tokens.push(token);
                break;

            default:
                let error = {
                    msg: `Unexpected character: ${c}`,
                    i: l.i,
                    line: l.line
                } as CompilingError;
                result.errors.push(error);

                token = {
                    kind: 'ILLEGAL',
                    value: c,
                    i: l.i,
                    line: l.line
                } as Token;
                result.tokens.push(token);
                l.i++;
                break;
        }
    }
    
    return result;
}

const opr = (l: LexingContext): Token | null => {
    for (let opr of OprTokenKind) {
        if (l.src.startsWith(opr, l.i)) {
            let token = {
                kind: opr,
                value: opr,
                i: l.i,
                line: l.line
            } as Token;
            l.i += opr.length;
            return token;
        }
    }
    return null;
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
        kind: 'NUMBER',
        value: value,
        i: l.i,
        line: l.line
    } as Token;
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
        if (value === key) {
            let token = {
                kind: key,
                value: value,
                i: l.i,
                line: l.line
            } as Token;
            l.i = cursor;
            return token;
        }
    }
    
    let token = {
        kind: 'IDENTIFIER' ,
        value: value,
        i: l.i,
        line: l.line
    } as Token;
    l.i = cursor;
    return token;
}
