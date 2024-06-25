import { type ExprNode, type UnaryNode, type BinaryNode, type GroupingNode, type ExprNodeKind, type IdentifierNode, type NilNode, type NumberNode, type FunCallNode } from './ast';
import { type CompilingError, type ParsingContext } from './types';

// expression     → equality ;
// equality       → comparison ( ( "!=" | "==" ) comparison )* ;
// comparison     → term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
// term           → factor ( ( "-" | "+" ) factor )* ;
// factor         → unary ( ( "/" | "*" ) unary )* ;
// unary          → ( "!" | "-" ) unary | primary ;
// primary        → NUMBER | "(" expression ")" | functionCall ;
// functionCall   → IDENTIFIER "(" arguments? ")" ;
// arguments      → expression ( "," expression )* ;

export const expression = (p: ParsingContext): ExprNode | CompilingError => {
    return equality(p);
}

const equality = (p: ParsingContext): ExprNode | CompilingError => {
    let left = comparison(p);
    if ('category' in left) return left; // propagate error

    let token = p.tokens[p.i];
    while (token && (token.kind === '!=' || token.kind === '==')) {
        p.i++; // consume the operator
        let right = comparison(p);
        if ('category' in right) return right; // propagate error

        left = {
            kind: 'BINARY',
            left: left,
            operator: token.kind,
            right: right,
            i: token.i,
            line: token.line,
        } as BinaryNode;
        token = p.tokens[p.i];
    }
    return left;
}

const comparison = (p: ParsingContext): ExprNode | CompilingError => {
    let left = term(p);
    if ('category' in left) return left; // propagate error

    let token = p.tokens[p.i];
    while (token && (token.kind === '>' || token.kind === '>=' || token.kind === '<' || token.kind === '<=')) {
        p.i++; // consume the operator
        let right = term(p);
        if ('category' in right) return right; // propagate error

        left = {
            kind: 'BINARY',
            left: left,
            operator: token.kind,
            right: right,
            i: token.i,
            line: token.line,
        } as BinaryNode;
        token = p.tokens[p.i];
    }
    return left;
}

const term = (p: ParsingContext): ExprNode | CompilingError => {
    let left = factor(p);
    if ('category' in left) return left; // propagate error

    let token = p.tokens[p.i];
    while (token && (token.kind === '+' || token.kind === '-')) {
        p.i++; // consume the operator
        let right = factor(p);
        if ('category' in right) return right; // propagate error

        left = {
            kind: 'BINARY',
            left: left,
            operator: token.kind,
            right: right,
            i: token.i,
            line: token.line,
        } as BinaryNode;
        token = p.tokens[p.i];
    }
    return left;
}

const factor = (p: ParsingContext): ExprNode | CompilingError => {
    let left = unary(p);
    if ('category' in left) return left; // propagate error

    let token = p.tokens[p.i];
    while (token && (token.kind === '*' || token.kind === '/')) {
        p.i++; // consume the operator
        let right = unary(p);
        if ('category' in right) return right; // propagate error

        left = {
            kind: 'BINARY',
            left: left,
            operator: token.kind,
            right: right,
            i: token.i,
            line: token.line,
        } as BinaryNode;
        token = p.tokens[p.i];
    }
    return left;
}

const unary = (p: ParsingContext): ExprNode | CompilingError => {
    let token = p.tokens[p.i];
    if (token.kind === '!' || token.kind === '-') {
        p.i++; // consume the operator
        let right = unary(p);
        if ('category' in right) return right; // propagate error
        return {
            kind: 'UNARY',
            operator: token.kind,
            right: right,
            i: token.i,
            line: token.line,
        } as UnaryNode;
    }
    return primary(p);
}
const primary = (p: ParsingContext): ExprNode | CompilingError => {
    let token = p.tokens[p.i];
    switch (token.kind) {
        case 'NUMBER':
            return number(p);
        case 'IDENTIFIER':
            return functionCall(p);
        case '(':
            return grouping(p);
        default:
            return {
                category: 'Parsing',
                msg: `Unexpected token: ${token.kind}`,
                i: token.i,
                line: token.line,
            } as CompilingError;
    }
}

const number = (p: ParsingContext): NumberNode => {
    let token = p.tokens[p.i];
    p.i++; // consume the number
    return {
        kind: 'NUMBER',
        value: token.value,
        i: token.i,
        line: token.line,
    } as NumberNode;
}

const functionCall = (p: ParsingContext): ExprNode | CompilingError => {
    let token = p.tokens[p.i];
    if (token.kind !== 'IDENTIFIER') {
        return {
            category: 'Parsing',
            msg: `Expected function name, found ${token.kind}`,
            i: token.i,
            line: token.line,
        } as CompilingError;
    }

    const functionName = token.value;
    p.i++; // consume the identifier

    token = p.tokens[p.i];
    if (token.kind !== '(') {
        return {
            category: 'Parsing',
            msg: `Expected '(', found ${token.kind}`,
            i: token.i,
            line: token.line,
        } as CompilingError;
    }
    p.i++; // consume the '('

    const args = [];
    if (p.tokens[p.i].kind !== ')') { // Check if there are any arguments
        // Parse the first argument
        let arg = expression(p);
        if ('category' in arg) return arg; // propagate error
        args.push(arg);

        // Now, parse subsequent arguments if any
        while (p.tokens[p.i] && p.tokens[p.i].kind === ',') {
            p.i++; // Correctly consume ',' before parsing the next argument
            arg = expression(p);
            if ('category' in arg) return arg; // propagate error
            args.push(arg);
        }
    }

    token = p.tokens[p.i];
    if (token.kind !== ')') {
        return {
            category: 'Parsing',
            msg: `Expected ')', found ${token.kind}`,
            i: token.i,
            line: token.line,
        } as CompilingError;
    }
    p.i++; // consume the ')'

    return {
        kind: 'FUNCALL',
        id: {
            kind: 'IDENTIFIER',
            isQualified: false,
            value: functionName,
        } as IdentifierNode,
        args: args,
        i: token.i,
        line: token.line,
    } as FunCallNode; // Assuming FunctionCallNode is defined appropriately
}

const grouping = (p: ParsingContext): GroupingNode | CompilingError => {
    p.i++; // consume the left parenthesis
    let expr = expression(p);
    if ('category' in expr) return expr; // propagate error

    let token = p.tokens[p.i];
    if (!token || token.kind !== ')') {
        return {
            category: 'Parsing',
            msg: `Expected right parenthesis, found ${token.kind}`,
            i: token.i,
            line: token.line,
        } as CompilingError;
    }
    p.i++; // consume the right parenthesis
    return {
        kind: 'GROUPING',
        expression: expr,
        i: token.i,
        line: token.line,
    } as GroupingNode;
}

