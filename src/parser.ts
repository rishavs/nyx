import type { Token } from "./tokens";
import type { CompilingError, ParsingContext } from "./types";

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
// Expected expectedTokenKind for expectedSyntax, but instead found got
// Expected expectedSyntax, but instead found got
type UnexpectedSyntax = {
    ok: false;
    expectedSyntax: string;
    expectedTokenKind?: string;
    got: Token;
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

type IntNode = BaseNode & {
    kind: 'INT';
    value: string;
}
type FloatNode = BaseNode & {
    kind: 'FLOAT';
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
type ExprNode = IntNode | FloatNode | IdentifierNode | FunCallNode
type StmtNode = FunCallNode

const failedExpectation = (p: ParsingContext, pos: number, expectedSyntax: string, expectedTokenKind?: string): UnexpectedSyntax =>  {
    return {
        ok: false,
        expectedSyntax,
        expectedTokenKind,
        got: p.tokens[pos]
    } as UnexpectedSyntax
}
// for  now we will only handle Root Node & Return Node
export const parse_file = (p: ParsingContext): RootNode | UnexpectedSyntax => {
    let root = {
        kind: 'ROOT',
        statements: [] as StmtNode[],
        ok: true,
        start: 0,
        end: 0,
    } as RootNode
    
    if (p.tokens.length == 0) return failedExpectation(p, false, 'Statement')

    while (p.i < p.tokens.length) {
        switch (p.tokens[p.i].kind) {
            case 'IDENTIFIER':
                let func = funcall(p)
                
                // If its an integer instead, return error as it is not a valid statement
                if (!func) return {ok:false, category: 'UnexpectedSyntax', msg: 'Expected a function call as a statement, but instead found an identifier', start: p.tokens[p.i].start, end: p.tokens[p.i].end, line: p.tokens[p.i].line}
                
                // If it is a function call, add it to the statements
                if (func.ok) {
                    root.statements.push(func)
                } else {
                // If it is an error, return the error
                    return func
                }
                break;

            default:
                return {ok:false, category: 'UnexpectedSyntax', msg: `Expected a Statement, but instead found token: ${p.tokens[p.i].kind}`, start: p.tokens[p.i].start, end: p.tokens[p.i].end, line: p.tokens[p.i].line}
        }
    }

    return root;
}


const expression = (p: ParsingContext): ExprNode | UnexpectedSyntax => {
    let cursor = p.i;
    let token = p.tokens[p.i];
    if (!token) return failedExpectation(p, false, 'Expression')

    switch (token.kind) {
        case 'FLOAT':
            return float(p);

        case 'INT':
            return int(p);

        // TODO - All failures should reset position
        case 'IDENTIFIER':
            let func = funcall(p);
            if (func.ok) {
                return func;
            } else {
                let id = identifier(p);
                if (id.ok) {
                    return id;
                } else {
                    return failedExpectation(p, 'Identifier or Function Call', 'IDENTIFIER')
                }
            }
        
        default:
            return failedExpectation(p, false, 'Expression')
    }
}


// As an ident token can either be an indent or a funcall, we will have to return null if it is not a funcall
const funcall = (p: ParsingContext): FunCallNode | UnexpectedSyntax => {
    let cursor = p.i;
    let err: UnexpectedSyntax

    let id = identifier(p);
    if (!id.ok) return id; // if it is not an identifier, propogate the error

    let token = p.tokens[cursor];
    const args: ExprNode[] = [];
    let func = {
        ok: true,
        kind: 'FUNCALL',
        id: id as IdentifierNode,
        args: args,
        start: token.start,
        end: token.end,
        line: token.line
    } as FunCallNode
    
    // if the current token is not '(', then it is not a function call
    token = p.tokens[cursor];
    if(!token || token.kind !== '(') {
        err = failedExpectation(p, false, 'Function Call')
        p.i = cursor; // reset the position
        return err
    }    

    cursor++; // else, consume the lparen
    token = p.tokens[cursor];

    if(!token) {
        err = failedExpectation(p, true, 'Function Call Arguments')
        p.i = cursor; // reset the position
        return err
    } // now we are at the first argument or ')'

    // if the next token is ')', then return the function without arguments
    if (token.kind === ')') {
        cursor++; // consume the ')'
        token = p.tokens[cursor];

        func.end = token.end;
        return func
    }

    // Time to parse the arguments
    // following comma is ok. Leading comma is not
    if (token.kind === ',') {
        err = failedExpectation(p, true, 'Function Call Arguments')
        p.i = cursor; // reset the position
        return err
    }

    // now we should only have arguments
    while (true) {
        token = p.tokens[cursor];
        if (!token) {
            err = failedExpectation(p, true, 'Function Call Arguments')
            p.i = cursor; // reset the position
            return err
        }

        if (token.kind === ',') {
            cursor++ // consume the ','
        } else if (token.kind === ')') {
            cursor++; // consume the ')'
            break; // End of arguments
        } else {
            let arg = expression(p);
            if (arg.ok) {
                args.push(arg as ExprNode) 
            } else {
                return arg // propogate the error
            }
        }
    }
    
    return func
}

const arguments = (p: ParsingContext): ExprNode[] | UnexpectedSyntax => {
    let cursor = p.i;
    let token = p.tokens[cursor];
    let args: ExprNode[] = [];
    if (!token || token.kind !== '(') return failedExpectation(p, false, 'Arguments', '(')

    cursor++; // consume the '('
    token = p.tokens[cursor];
    if (!token) return failedExpectation(p, true, 'Arguments')

    if (token.kind === ')') {
        cursor++; // consume the ')'
        p.i = cursor;
        return args;
    }

    while (true) {
        token = p.tokens[cursor];
        if (!token) return failedExpectation(p, true, 'Arguments')

        if (token.kind === ',') {
            cursor++; // consume the ','
        } else if (token.kind === ')') {
            cursor++; // consume the ')'
            p.i = cursor;
            return args;
        } else {
            let arg = expression(p);
            if (arg.ok) {
                args.push(arg as ExprNode) 
            } else {
                return arg // propogate the error
            }
        }
    }
} 

const identifier = (p: ParsingContext): IdentifierNode | UnexpectedSyntax => {
    let cursor = p.i;
    let token = p.tokens[cursor];
    if (!token || token.kind !== 'IDENTIFIER') return failedExpectation(p, false, 'Identifier', 'IDENTIFIER')

    let id = {
        kind: 'IDENTIFIER',
        isQualified: false,
        value: token.value,
        start: token.start,
        end: token.end,
        line: token.line,
    } as IdentifierNode
    cursor++; // consume the identifier

    // handle qualified identifiers
    while (true) {
        token = p.tokens[cursor];
        if (token && token.kind === '.') {
            cursor++; // consume the '.'
            token = p.tokens[cursor + 1];
            if (!token || token.kind !== 'IDENTIFIER') {
                return failedExpectation(p, true, 'Qualified Identifier', 'IDENTIFIER')
            }     
            id.isQualified = true;
            id.value += '.' + token.value;
            id.end = token.end;
            cursor++; // consume the identifier
        } else {
            break;
        }
    }
    p.i = cursor;
    return id;
}

const int = (p: ParsingContext): IntNode | null => {
    let token = p.tokens[p.i];
    if (token && token.kind === 'INT') {
        p.i++; // consume the number
        return {
            kind: 'INT',
            value: token.value,
            start: token.start,
            end: token.end,
            line: token.line
        } as IntNode
    }
    
    return null
}

const float = (p: ParsingContext): FloatNode | null => {
    let token = p.tokens[p.i];
    if (token && token.kind === 'Float') {
        p.i++; // consume the number
        return {
            kind: 'FLOAT',
            value: token.value,
            start: token.start,
            end: token.end,
            line: token.line
        } as FloatNode
    }
    
    return null
}