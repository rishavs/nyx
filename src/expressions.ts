import { type ExprNode, type UnaryNode, type BinaryNode, type GroupingNode, ExprNodeKind, type IdentifierNode, type NilNode, type NumberNode } from './ast';
import { OprTokenKind, SpecialTokenKind } from './tokens';
import { type CompilingError, type ParsingContext } from './types';

export const parse_expression = (p: ParsingContext): ExprNode | CompilingError => {
    let token = p.tokens[p.i];
    let left, right : ExprNode | CompilingError;

    switch (token.kind) {
        // Parse Identifiers & Function calls
        case SpecialTokenKind.IDENTIFIER:
            return {
                kind: ExprNodeKind.IDENTIFIER,
                isQualified: false, // or true, depending on your language semantics
                name: token.value,
            } as IdentifierNode;
        
        // Parse Literals
        case SpecialTokenKind.NUMBER:
            let nextToken = p.tokens[p.i + 1];
            if (nextToken && (
                nextToken.kind === OprTokenKind.PLUS)
            ) { 
                return parse_binary(p)
            }
            return {
                kind: ExprNodeKind.NUMBER,
                value: token.value,
            } as NumberNode;
    
        // Parse Unary Expressions
        case OprTokenKind.NOT:
        case OprTokenKind.NEGATE:
            p.i++; // consume the operator
            right = parse_expression(p);
            if ('category' in right) return right; // propagate error. no way to check types
            return {
                kind: ExprNodeKind.UNARY,
                operator: token.kind,
                right: right,
            } as UnaryNode;

        // Parse Binary Expressions
        case OprTokenKind.EQUALS:
        case OprTokenKind.PLUS:
            p.i++; // consume the operator
            left = parse_expression(p);
            if ('category' in left) return left; // propagate error
            right = parse_expression(p);
            if ('category' in right) return right; // propagate error
            return {
                kind: ExprNodeKind.BINARY,
                left: left,
                operator: token.kind,
                right: right,
            } as BinaryNode;
                
        // Parse Grouping Expressions
        case SpecialTokenKind.LPAREN:
            p.i++; // consume the left parenthesis
            let expr = parse_expression(p);
            if ('category' in expr) return expr; // propagate error
            if (p.tokens[p.i].kind !== SpecialTokenKind.RPAREN) {
                return {
                    category: 'Parsing',
                    msg: `Expected right parenthesis, found ${p.tokens[p.i].kind}`,
                    i: p.tokens[p.i].i,
                    line: p.tokens[p.i].line,
                } as CompilingError;
            }
            p.i++; // consume the right parenthesis
            return {
                kind: ExprNodeKind.GROUPING,
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

const parse_binary = (p: ParsingContext): ExprNode | CompilingError => {
    let left, right : ExprNode | CompilingError;
    let token = p.tokens[p.i];
    p.i++; // consume the operator
    left = parse_expression(p);
    if ('category' in left) return left; // propagate error
    right = parse_expression(p);
    if ('category' in right) return right; // propagate error
    return {
        kind: ExprNodeKind.BINARY,
        left: left,
        operator: token.kind,
        right: right,
    } as BinaryNode;
}