import { type Token, type LexingContext, KwdTokenKind, SpecialTokenKind, OprTokenKind, type LexingResult } from "./types";

export const lex_number = (l: LexingContext): Token => {
    let start = l.i;
    let isFloat = false;
    let value = '';
    while (l.i < l.src.length && /[0-9_\.]/.test(l.src[l.i])) {
        if (l.src[l.i] === '_') {
            l.i++;
            continue;
        }
        if (l.src[l.i] === '.') {
            if (isFloat) {
                break;
            }
            isFloat = true;
        }
        value += l.src[l.i];
        l.i++;
    }
    return {
        kind: SpecialTokenKind.NUMBER,
        value: value,
        i: start,
        line: l.line
    };
}

export const lex_ws = (l: LexingContext): Token => {
    let start = l.i;
    while (l.i < l.src.length && /[ \n\t]/.test(l.src[l.i])) {
        if (l.src[l.i] === '\n') {
            l.line++;
        }
        l.i++;
    }
    
    return {
        kind: SpecialTokenKind.WS,
        value: '',
        i: start,
        line: l.line
    };
}

export const lex_idOrKwd = (l: LexingContext): Token => {
    let start = l.i;
    while (l.i < l.src.length && /[a-zA-Z_0-9]/.test(l.src[l.i])) {
        l.i++;
    }
    let token: Token = {
        kind: SpecialTokenKind.IDENTIFIER,
        value: l.src.slice(start, l.i),
        i: start,
        line: l.line
    };

    // iterate over the FixedTokenKind enum
    for (let key of Object.keys(KwdTokenKind)) {
        if (token.value === KwdTokenKind[key as keyof typeof KwdTokenKind]) {
            token.kind = KwdTokenKind[key as keyof typeof KwdTokenKind];
            token.value = ''
        }
    }
    
    return token
}

export const lex_equalsOrAssign = (l: LexingContext): Token => {
    let start = l.i;
    if (l.src.startsWith('==', l.i)) {
        l.i += 2;
        return {
            kind: OprTokenKind.EQUALS,
            value: '==',
            i: start,
            line: l.line
        };
    } else {
        l.i++;
        return {
            kind: OprTokenKind.ASSIGN,
            value: '=',
            i: start,
            line: l.line
        };
    }
}

export const lex_file = (l: LexingContext): LexingResult => {
    let result : LexingResult= {
        tokens: [],
        errors: []
    };
    while (l.i < l.src.length) {
        let c = l.src[l.i];
        if (c === ' ' || c === '\t' || c === '\n') {
            let token = lex_ws(l);
            // if (token) tokens.push(token);

        } else if (/[a-zA-Z_]/.test(c)) {
            let token = lex_idOrKwd(l);
            if (token) result.tokens.push(token);
        } else if (/[0-9]/.test(c)) {
            let token = lex_number(l);
            if (token) result.tokens.push(token); 

        } else if (c === '=') {
            let token = lex_equalsOrAssign(l);
            if (token) result.tokens.push(token);
        } else if (c === '+') {
            let token: Token = {
                kind: OprTokenKind.PLUS,
                value: '+',
                i: l.i,
                line: l.line
            };
            result.tokens.push(token);
            l.i++;                                                                                                                                                                  
        } else {
            let error = {
                category: 'SyntaxError',
                msg: `unexpected character '${c}'`,
                i: l.i,
                line: l.line
            };
            result.errors.push(error);

            let token: Token = {
                kind: SpecialTokenKind.ILLEGAL,
                value: c,
                i: l.i,
                line: l.line
            };
            result.tokens.push(token);

            l.i++;
        }
    }
    return result;
}
