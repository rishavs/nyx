import { type ExprNode, type UnaryNode, type BinaryNode, type GroupingNode, type ExprNodeKind, type IdentifierNode, type NilNode, type NumberNode } from './ast';
import { type CompilingError, type ParsingContext } from './types';

// expression     → equality ;
// equality       → comparison ( ( "!=" | "==" ) comparison )* ;
// comparison     → term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
// term           → factor ( ( "-" | "+" ) factor )* ;
// factor         → unary ( ( "/" | "*" ) unary )* ;
// unary          → ( "!" | "-" ) unary | primary ;
// primary        → NUMBER | "(" expression ")" ;

// TODO - ad the i, & line info to the nodes

export const expression = (p: ParsingContext): ExprNode | CompilingError => {
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
        } as UnaryNode;
    }
    return primary(p);
}
const primary = (p: ParsingContext): ExprNode | CompilingError => {
    let token = p.tokens[p.i];
    switch (token.kind) {
        case 'NUMBER':
            return number(p);
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
    } as GroupingNode;
}




// export const parse_expression = (p: ParsingContext): ExprNode | CompilingError => {
//     let token = p.tokens[p.i];
//     let left, right : ExprNode | CompilingError;

//     switch (token.kind) {
//         // Parse Identifiers & Function calls
//         case 'IDENTIFIER':
//             return {
//                 kind: 'IDENTIFIER',
//                 isQualified: false, // or true, depending on your language semantics
//                 name: token.value,
//             } as IdentifierNode;
        
//         // Parse Literals
//         case 'NUMBER':
//             let number = {
//                 kind: 'NUMBER',
//                 value: token.value,
//             } as NumberNode;
//             p.i++; // consume the number

//             token = p.tokens[p.i];
//             if (token && (
//                 token.kind === '+')
//             ) { 
//                 return parse_binary(p, number)
//             }
//             return number;
    
//         // Parse Unary Expressions
//         case '!':
//         case '-':
//             p.i++; // consume the operator
//             right = parse_expression(p);
//             if ('category' in right) return right; // propagate error. no way to check types
//             return {
//                 kind: 'UNARY',
//                 operator: token.kind,
//                 right: right,
//             } as UnaryNode;

        
                
//         // Parse Grouping Expressions
//         case '(':
//             p.i++; // consume the left parenthesis
//             let grouping = parse_grouping(p);
//             return grouping;

//         default:
//             return {
//                 category: 'Parsing',
//                 msg: `Unexpected token: ${token.kind}`,
//                 i: token.i,
//                 line: token.line,
//             } as CompilingError;
//     }
// }

// const parse_binary = (p: ParsingContext, left: ExprNode): ExprNode | CompilingError => {
    
//     let currentToken = p.tokens[p.i];
//     p.i++; // consume the operator

//     let right = parse_expression(p);
//     if ('category' in right) return right; // propagate error
//     return {
//         kind: 'BINARY',
//         left: left,
//         operator: currentToken.value,
//         right: right,
//     } as BinaryNode;
// }

// const parse_grouping = (p: ParsingContext): ExprNode | CompilingError => {
//     let expr = parse_expression(p);
//     if ('category' in expr) return expr; // propagate error
//     if (p.tokens[p.i].kind !== ')') {
//         return {
//             category: 'Parsing',
//             msg: `Expected right parenthesis, found ${p.tokens[p.i].kind}`,
//             i: p.tokens[p.i].i,
//             line: p.tokens[p.i].line,
//         } as CompilingError;
//     }
//     p.i++; // consume the right parenthesis
//     return {
//         kind: 'GROUPING',
//         expression: expr,
//     } as GroupingNode;
// }