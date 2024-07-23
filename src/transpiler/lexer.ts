import { KwdTokenKind, OprTokenKind, type Token } from "./tokens";
import { type LexingContext } from "./defs";
import { IllegalTokenError, UnhandledError, UnclosedDelimiterError, type TranspilingError } from "./errors";

// Character definitions
// TODO - lexer broken for consequtive operators

const isAlphabet = (c: string): boolean =>  
    'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ'.includes(c);

const isDigit = (c: string): boolean => 
    '0123456789'.includes(c);

const isSpecialChar = (c: string): boolean =>
    !isAlphabet(c) && !isDigit(c) && !' \n\t'.includes(c) && c !== '_';

export const lex_file = (l: LexingContext): Token[] | TranspilingError[] => {
    let tokens: Token[] = []
    let errors: TranspilingError[] = []

    while (l.i < l.src.length && errors.length === 0) {
        let c = l.src[l.i];
        let token: Token | null;

        switch (true) {
            case ' \n\t\r'.includes(c):
                ws(l);
                break;

            case c === '-':
                comment(l);
                break;

            case isAlphabet(c) || c === '_':
                token = kwdOrId(l);
                tokens.push(token);
                break;

            case isDigit(c):
                token = number(l);
                tokens.push(token);
                break;

            case isSpecialChar(c):
                token = opr(l);
                if (token) {
                    tokens.push(token);
                } else {
                    throw new IllegalTokenError(c, l.i, l.line);
                }               
                break;

            default:
                let error = new IllegalTokenError(c, l.i, l.line);
                errors.push(error);
                throw new IllegalTokenError(c, l.i, l.line);
        }
    }
    
    if (errors.length == 0) {
        return tokens;
    }
    
    return errors;
}


const comment = (l: LexingContext) => {
    let cursor = l.i;
    if (l.src.startsWith('-[', cursor)) {
        let closing = l.src.indexOf(']-', cursor);
        if (closing === -1) {
            throw new UnclosedDelimiterError('Multiline Comment', ']-', l.i, l.line);
        } 
        l.i = closing + 2;
        
    } else if (l.src.startsWith('--', cursor)) {
        let closing = l.src.indexOf('\n', cursor);
        l.i = closing === -1 ? l.src.length : closing;

    } 
}

const opr = (l: LexingContext): Token | null => {
    let token = {
        kind: 'ILLEGAL',
        value: '',
        start: l.i,
        end: l.i,
        line: l.line
    } as Token;
    
    for (let opr of OprTokenKind) {
        if (l.src.startsWith(opr, l.i)) {
            token.kind = opr;
            token.value = opr;
            l.i += opr.length;
            return token;
        }
    }
    return null;
}

const number = (l: LexingContext): Token => {
    let token = {
        kind: 'INT',
        value: '',
        start: l.i,
        end: l.i,
        line: l.line
    } as Token;

    token.end = l.i;
    let isFloat = false;
    while (
        token.end < l.src.length && 
        (isDigit(l.src[token.end]) || l.src[token.end] === '.' || l.src[token.end] === '_' )
    ) {
        if (l.src[token.end] === '_') {
            token.end++;
            continue;
        }
        if (l.src[token.end] === '.') {
            if (isFloat) {
                break;
            }
            isFloat = true;
            token.kind = 'FLOAT';
        }
        token.value += l.src[token.end];
        token.end++;
    }
    l.i = token.end;
    token.end--;
    return token;
}

const ws = (l: LexingContext) => {
    let cursor = l.i;
    while (cursor < l.src.length && ' \n\t\r'.includes(l.src[cursor]) ){
        if (l.src[cursor] === '\n') {
            l.line++;
        }
        cursor++;
    }
    l.i = cursor;
}

const kwdOrId = (l: LexingContext): Token => {
    let token = {
        kind: 'IDENTIFIER' ,
        value: '',
        start: l.i,
        end: l.i,
        line: l.line
    } as Token;

    while (
        token.end < l.src.length && 
        (isAlphabet(l.src[token.end]) || isDigit(l.src[token.end]) || l.src[token.end] === '_' )
    ) {
        token.end++;
    }
    token.value = l.src.slice(l.i, token.end);

    // iterate over the KwdTokenKind array and check if the token is a keyword
    for (let kwd of KwdTokenKind) {
        if (token.value === kwd) {
            token.kind = kwd as Token['kind'];
            break;
        }
    }
    l.i = token.end;
    token.end--;
    return token;
}
