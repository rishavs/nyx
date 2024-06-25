import { type ExprNode, type UnaryNode, type BinaryNode, type GroupingNode, type ExprNodeKind, type IdentifierNode, type NilNode, type NumberNode, type FunCallNode } from './ast';
import { type CompilingError, type ExprParsingResult, type ParsingContext } from './types';

// expression     → equality ;
// equality       → comparison ( ( "!=" | "==" ) comparison )* ;
// comparison     → term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
// term           → factor ( ( "-" | "+" ) factor )* ;
// factor         → unary ( ( "/" | "*" ) unary )* ;
// unary          → ( "!" | "-" ) unary | primary ;
// primary        → NUMBER | "(" expression ")" | functionCall ;
// functionCall   → IDENTIFIER "(" arguments? ")" ;
// arguments      → expression ( "," expression )* ;

export const expression = (p: ParsingContext): ExprParsingResult => {
    return primary(p);
}

// const equality = (p: ParsingContext): ExprParsingResult => {
//     let left = comparison(p);
//     if ('category' in left) return left; // propagate error

//     let token = p.tokens[p.i];
//     while (token && (token.kind === '!=' || token.kind === '==')) {
//         p.i++; // consume the operator
//         let right = comparison(p);
//         if ('category' in right) return right; // propagate error

//         left = {
//             kind: 'BINARY',
//             left: left,
//             operator: token.kind,
//             right: right,
//             i: token.i,
//             line: token.line,
//         } as BinaryNode;
//         token = p.tokens[p.i];
//     }
//     return left;
// }

// const comparison = (p: ParsingContext): ExprParsingResult => {
//     let left = term(p);
//     if ('category' in left) return left; // propagate error

//     let token = p.tokens[p.i];
//     while (token && (token.kind === '>' || token.kind === '>=' || token.kind === '<' || token.kind === '<=')) {
//         p.i++; // consume the operator
//         let right = term(p);
//         if ('category' in right) return right; // propagate error

//         left = {
//             kind: 'BINARY',
//             left: left,
//             operator: token.kind,
//             right: right,
//             i: token.i,
//             line: token.line,
//         } as BinaryNode;
//         token = p.tokens[p.i];
//     }
//     return left;
// }

// const term = (p: ParsingContext): ExprParsingResult => {
//     let left = factor(p);
//     if ('category' in left) return left; // propagate error

//     let token = p.tokens[p.i];
//     while (token && (token.kind === '+' || token.kind === '-')) {
//         p.i++; // consume the operator
//         let right = factor(p);
//         if ('category' in right) return right; // propagate error

//         left = {
//             kind: 'BINARY',
//             left: left,
//             operator: token.kind,
//             right: right,
//             i: token.i,
//             line: token.line,
//         } as BinaryNode;
//         token = p.tokens[p.i];
//     }
//     return left;
// }

// const factor = (p: ParsingContext): ExprParsingResult => {
//     let left = unary(p);
//     if ('category' in left) return left; // propagate error

//     let token = p.tokens[p.i];
//     while (token && (token.kind === '*' || token.kind === '/')) {
//         p.i++; // consume the operator
//         let right = unary(p);
//         if ('category' in right) return right; // propagate error

//         left = {
//             kind: 'BINARY',
//             left: left,
//             operator: token.kind,
//             right: right,
//             i: token.i,
//             line: token.line,
//         } as BinaryNode;
//         token = p.tokens[p.i];
//     }
//     return left;
// }

// const unary = (p: ParsingContext): ExprParsingResult => {
//     let token = p.tokens[p.i];
//     if (token.kind === '!' || token.kind === '-') {
//         p.i++; // consume the operator
//         let right = unary(p);
//         if ('category' in right) return right; // propagate error
//         return {
//             kind: 'UNARY',
//             operator: token.kind,
//             right: right,
//             i: token.i,
//             line: token.line,
//         } as UnaryNode;
//     }
//     return primary(p);
// }


const primary = (p: ParsingContext): ExprParsingResult => {
    let token = p.tokens[p.i];
    switch (token.kind) {
        case 'NUMBER':
            return number(p);

        // TODO - separate identifier from function call
        case 'IDENTIFIER':
            return functionCall(p);
        case '(':
            return grouping(p);
        default:
            token = p.tokens[p.i];
            return {
                ok: false,
                result: {
                    category: 'Parsing',
                    msg: `Unexpected token: ${token.kind}`,
                    i: token.i,
                    line: token.line,
                } as CompilingError
            }
    }
}

const number = (p: ParsingContext): ExprParsingResult => {
    let token = p.tokens[p.i];
    p.i++; // consume the number

    return {
        ok: true,
        result: {
            kind: 'NUMBER',
            value: token.value,
            i: token.i,
            line: token.line
        } as NumberNode
    } as ExprParsingResult
}

const functionCall = (p: ParsingContext): ExprParsingResult => {
    let token = p.tokens[p.i];

    const functionName = token.value;
    p.i++; // consume the identifier

    // If there is no '(' after the identifier, then it is not a function call
    // instead it is just an identifier
    token = p.tokens[p.i];
    if (!token || token.kind !== '(') {
        return {
            ok: true,
            result: {
                kind: 'IDENTIFIER',
                isQualified: false,
                value: functionName,
                i: token.i,
                line: token.line,
            } as IdentifierNode
        }
    }
    p.i++; // consume the '('

    const args: ExprNode[] = [];
    if (p.tokens[p.i].kind !== ')') { // Check if there are any arguments
        // Parse the first argument
        let arg = expression(p);
        if (!arg.ok) return arg; // propagate error
        
        if (arg.result) {
            args.push(arg.result as ExprNode);

            // Now, parse subsequent arguments if any
            while (p.tokens[p.i] && p.tokens[p.i].kind === ',') {
                p.i++; // Correctly consume ',' before parsing the next argument
                arg = expression(p);
                if (!arg.ok) return arg; // propagate error

                if (!arg.result) {
                    return {
                        ok: false,
                        result: {
                            category: 'Parsing',
                            msg: `Expected an argument after the comma, but found none`,
                            i: token.i,
                            line: token.line,
                        } as CompilingError
                    }
                }

                args.push(arg.result as ExprNode);
            }
        }
    }

    token = p.tokens[p.i];
    if (!token || token.kind !== ')') {
        return {
            ok: false,
            result: {
                category: 'Parsing',
                msg: `Expected ')', found ${token.kind}`,
                i: token.i,
                line: token.line,
            } as CompilingError
        }
    }
    p.i++; // consume the ')'

    let idNode = {
        kind: 'IDENTIFIER',
        isQualified: false,
        value: functionName,
        i: token.i,
        line: token.line,
    } as IdentifierNode;

    let funcallNode = {
        kind: 'FUNCALL',
        id: idNode,
        args: args,
        i: token.i,
        line: token.line,
    } as FunCallNode;

    return {
        ok: true,
        result: funcallNode
    }
}

const grouping = (p: ParsingContext): ExprParsingResult => {
    p.i++; // consume the left parenthesis
    let token = p.tokens[p.i];

    let expr = expression(p);
    if (!expr.ok) return expr; // propagate error
    if (!expr.result) { // if the expression is null, return error
        return {
            ok: false,
            result: {
                category: 'Parsing',
                msg: `Expected an expression inside the parentheses, but found none`,
                i: token.i,
                line: token.line,
            } as CompilingError
        }
    }

    // If expression parsed, the next token should be a right parenthesis
    token = p.tokens[p.i];
    if (!token || token.kind !== ')') {
        return {
            ok: false,
            result: {
                category: 'Parsing',
                msg: `Expected right parenthesis, found ${token.kind}`,
                i: token.i,
                line: token.line,
            } as CompilingError
        }
    }
    p.i++; // consume the right parenthesis
    return {
        ok: true,
        result: {
            kind: 'GROUPING',
            expression: expr.result as ExprNode,
            i: token.i,
            line: token.line,
        } as GroupingNode
    }
}

