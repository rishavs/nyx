import { type ExprNode, type UnaryNode, type BinaryNode, type GroupingNode, type ExprNodeKind, type IdentifierNode, type NilNode, type NumberNode } from './ast';
import { type CompilingError, type ParsingContext } from './types';

export const parse_expression = (p: ParsingContext): ExprNode | CompilingError => {
    let token = p.tokens[p.i];
    let left, right : ExprNode | CompilingError;

    switch (token.kind) {
        // Parse Identifiers & Function calls
        case 'IDENTIFIER':
            return {
                kind: 'IDENTIFIER',
                isQualified: false, // or true, depending on your language semantics
                name: token.value,
            } as IdentifierNode;
        
        // Parse Literals
        case 'NUMBER':
            let number = {
                kind: 'NUMBER',
                value: token.value,
            } as NumberNode;
            p.i++; // consume the number

            token = p.tokens[p.i];
            if (token && (
                token.kind === '+')
            ) { 
                return parse_binary(p, number)
            }

            return number;
    
        // Parse Unary Expressions
        case '!':
        case '-':
            p.i++; // consume the operator
            right = parse_expression(p);
            if ('category' in right) return right; // propagate error. no way to check types
            return {
                kind: 'UNARY',
                operator: token.kind,
                right: right,
            } as UnaryNode;

        
                
        // Parse Grouping Expressions
        case '(':
            p.i++; // consume the left parenthesis
            let expr = parse_expression(p);
            if ('category' in expr) return expr; // propagate error
            if (p.tokens[p.i].kind !== ')') {
                return {
                    category: 'Parsing',
                    msg: `Expected right parenthesis, found ${p.tokens[p.i].kind}`,
                    i: p.tokens[p.i].i,
                    line: p.tokens[p.i].line,
                } as CompilingError;
            }
            p.i++; // consume the right parenthesis
            return {
                kind: 'GROUPING',
                expression: expr,
            } as GroupingNode;

        default:
            return {
                category: 'Parsing',
                msg: `Unexpected token: ${token.kind}`,
                i: token.i,
                line: token.line,
            } as CompilingError;
    }
}

const parse_binary = (p: ParsingContext, left: ExprNode): ExprNode | CompilingError => {
    
    let currentToken = p.tokens[p.i];
    p.i++; // consume the operator

    let right = parse_expression(p);
    if ('category' in right) return right; // propagate error
    return {
        kind: 'BINARY',
        left: left,
        operator: currentToken.value,
        right: right,
    } as BinaryNode;
}