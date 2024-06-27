import type { Token } from "./tokens";
import type { ParsingContext } from "./types";

class ParsingError extends Error {
    i: number;
    line: number;
    constructor(message: string, i: number, line: number) {
        super(message);
        this.name = this.constructor.name;
        this.i = i;
        this.line = line;
    }
}

type SyntaxError = {
    ok: false;
    message: string;
    i: number;
    line: number;
}

type BaseNode = {
    ok:true;
    start: number;
    end: number;
    line: number;
}

type IdentifierNode = BaseNode & {
    kind: 'IDENTIFIER';
    isQualified: boolean;
    value: string;
}

type NumberNode = BaseNode & {
    kind: 'NUMBER';
    value: string;
}

type FunCallNode = BaseNode & {
    kind: 'FUNCALL';
    id: IdentifierNode;
    args: ExprNode[];
}

type RootNode = BaseNode & {
    kind: 'ROOT';
    statements: StmtNode[];
}
type ExprNode = NumberNode | IdentifierNode | FunCallNode
type StmtNode = FunCallNode

// If we just need to advance without expectations, we can just do p.i++
const advance = (p: ParsingContext, expected: string): Token => {
    let token = p.tokens[p.i];
    let nextToken = p.tokens[p.i + 1];
    if (!nextToken) 
        throw new ParsingError(`Expecting a ${expected}, but reached end of input`, token.i, token.line);
    
    p.i++;
    return p.tokens[p.i];
}    
const advanceAsExpected = (p: ParsingContext, expectedToken: string): Token => {
    let token = p.tokens[p.i];
    let nextToken = p.tokens[p.i + 1];
    
    if (!nextToken) 
        throw new ParsingError(`Expecting a ${expectedToken}, but reached end of input`, token.i, token.line);
    
    if (token.kind !== expectedToken) 
        throw new ParsingError(`Expecting a ${expectedToken}, but instead found ${token.kind}`, token.i, token.line);

    p.i++;    
    return p.tokens[p.i];
}

// for  now we will only handle Root Node & Return Node
export const parse_file = (p: ParsingContext): RootNode | SyntaxError => {
    let root = {
        kind: 'ROOT',
        statements: [] as StmtNode[],
        ok: true,
        start: 0,
        end: 0,
    } as RootNode
    
    if (p.tokens.length == 0) return { ok: false, message: 'No tokens to parse', i: 0, line: 0 }

    while (p.i < p.tokens.length) {
        switch (p.tokens[p.i].kind) {
            case 'IDENTIFIER':
                root.statements.push(funcall(p) as FunCallNode)
                break;

            default:
                throw new ParsingError(`Expected a Statement, but instead found token: ${p.tokens[p.i].kind}`, p.tokens[p.i].i, p.tokens[p.i].line);
        }
        p.i++;
    }

    return root;
}

// As an ident token can either be an indent or a funcall, we will have to return null if it is not a funcall
const funcall = (p: ParsingContext): FunCallNode | SyntaxError | null => {
    let id = identifier(p) as IdentifierNode;
    let token = p.tokens[p.i]; // the identifier

    // if the next token is not '(', then it is not a function call
    if (!token || token.kind !== '(') {
        return null
    }

    const args: ExprNode[] = [];
    let func = {
        ok: true,
        kind: 'FUNCALL',
        id: id,
        args: args,
        start: token.i,
        end: token.i,
        line: token.line
    } as FunCallNode

    // consume the LParen. 
    if(!p.tokens[p.i + 1] ) {
        return {ok:false, message: 'Expected Function arguments but instead reached end of input', i: token.i, line: token.line}
    }
    p.i++; // now we are at the first argument or ')'

    // if the next token is ')', then return the function without arguments
    if (p.tokens[p.i].kind === ')') {
        p.i++; // consume the ')'
        func.end = p.tokens[p.i].i;
        return func
    }

    // following comma is ok. Leading comma is not
    if (token.kind !== ',') 
        throw new ParsingError(`Expected a "(", but instead found ${token.kind}`, token.i, token.line);

    // now we should only have arguments
    while (true) {
        if (p.tokens[p.i].kind === ',') {
            p.i++ // consume the ','
            
        } else if (p.tokens[p.i].kind === ')') {
            p.i++; // consume the ')'
            break; // End of arguments
        } else {
            let arg = expression(p);
            if (arg) args.push(arg as ExprNode);        
        }
    }
    
    return func
}

const expression = (p: ParsingContext): ExprNode => {
    let token = p.tokens[p.i];
    if (!token) throw new ParsingError('Expected Expression, but instead reached end of input', 0, 0);

    switch (token.kind) {
        case 'NUMBER':
            return number(p);

        case 'IDENTIFIER':
            return funcall(p);
        
        default:
            throw new ParsingError(`Unexpected token in expression: ${token.kind}`, token.i, token.line);
    }
}

const identifier = (p: ParsingContext): IdentifierNode => {
    let token = p.tokens[p.i];
    let id = {
        kind: 'IDENTIFIER',
        isQualified: false,
        value: token.value,
        i: token.i,
        line: token.line,
    } as IdentifierNode
    p.i++; // consume the identifier

    // handle qualified identifiers
    while (true) {
        token = p.tokens[p.i ];

        if (!token) break;
        if (token.kind === '.') {
            id.isQualified = true;
            token = advance(p, '.'); // consume the '.'
            token = advance(p, 'IDENTIFIER'); // consume the identifier
            id.value += '.' + token.value;
            id.i = token.i;
            id.line = token.line;
        } else {
            break;
        }
    }
    return id;
}

const number = (p: ParsingContext): NumberNode => {
    let token = p.tokens[p.i];
    p.i++; // consume the number

    return {
        kind: 'NUMBER',
        value: token.value,
        i: token.i,
        line: token.line
    } as NumberNode
}