import type { ParsingContext } from "./types";

// Extend Error
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
  

type BaseNode = {
    ok:true;
    value: string;
    i: number;
    line: number;
}

type IdentifierNode = BaseNode & {
    kind: 'IDENTIFIER';
    isQualified: boolean;
}

type NumberNode = BaseNode & {
    kind: 'NUMBER';
}

type FunCallNode = BaseNode & {
    kind: 'FUNCALL';
    id: IdentifierNode;
    args: ExprNode[];
}

type RootNode = {
    kind: 'ROOT';
    statements: StmtNode[];
}
type ExprNode = NumberNode | IdentifierNode | FunCallNode
type StmtNode = FunCallNode


// for  now we will only handle Root Node & Return Node
export const parse_file = (p: ParsingContext): RootNode => {
    let root = {
        kind: 'ROOT',
        statements: []
    } as RootNode

    
    if (p.tokens.length === 0) return {
        ok: false,
        msg: 'No tokens to parse',
        i: 0,
        line: 0
    }

    while (p.i < p.tokens.length) {
        switch (p.tokens[p.i].kind) {
            case 'IDENTIFIER':
                let result = funcall(p);
                if (!result.ok) return result;
                root.statements.push(result as FunCallNode);
                break;

            default:
                return {
                    ok: false,
                    msg: `Expecting a statement, but instead found ${p.tokens[p.i].kind}`,
                    i: p.tokens[p.i].i,
                    line: p.tokens[p.i].line
                }
        }
        p.i++;
    }

    return root;
}

const funcall = (p: ParsingContext): FunCallNode | ParsingError  => {
    let token = p.tokens[p.i];

    const functionName = token.value;
    // p.i++; // consume the identifier
    let nextToken = p.tokens[p.i + 1];

    // If there is no '(' after the identifier, then it is not a function call
    // instead it is just an identifier
    if (!token) {
        p.i--; // backtrack
        token = p.tokens[p.i];
        return {
            ok: false,
            msg: 'Unexpeted end of file',
            i: token.i,
            line: token.line
        } as ParsingError
    }

    if (!token || token.kind !== '(') {
        return {
            ok: false,
            msg: 'Expected "(" after function name',
            i: token.i,
            line: token.line
        }
    }
    p.i++; // consume the '('

    const args: ExprNode[] = [];
    while (p.tokens[p.i].kind !== ')') {
        let arg = expression(p);
        if (arg && !arg.ok) return arg;
        if (arg) args.push(arg as ExprNode);

        if (p.tokens[p.i].kind === ',') p.i++; // consume the ','
    }
    p.i++; // consume the ')'

    return {
        ok: true,
        kind: 'FUNCALL',
        id: {
            kind: 'IDENTIFIER',
            isQualified: false,
            value: functionName,
            i: token.i,
            line: token.line,
        } as IdentifierNode,
        args
    } as FunCallNode
}

const expression = (p: ParsingContext): ExprNode | ParsingError | null => {
    let token = p.tokens[p.i];

    switch (token.kind) {
        case 'NUMBER':
            return number(p);

        case 'IDENTIFIER':
            return funcall(p);
        
        default:
            token = p.tokens[p.i];
            return {
                ok: false,
                msg: `Unexpected token: ${token.kind}`,
                i: token.i,
                line: token.line
            } as ParsingError
    }
}

const number = (p: ParsingContext): NumberNode => {
    let token = p.tokens[p.i];
    p.i++; // consume the number

    return {
        ok: true,
        kind: 'NUMBER',
        value: token.value,
        i: token.i,
        line: token.line
    } as NumberNode
}