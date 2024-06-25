import { SpecialNodeKind, type ExprNode, type IdentifierNode, type NumberNode, type RootNode } from "./ast";
import { KwdTokenKind } from "./tokens";
import type { ParsingContext, ParsingResult } from "./types";

// for  now we will only handle Root Node & Return Node
export const parse_file = (p: ParsingContext): ParsingResult => {
    let result : ParsingResult = {
        root: {
            kind: SpecialNodeKind.ROOT,
            statements: []
        } as RootNode,
        errors: []
    };

    if (p.tokens.length === 0) {
        let error = {
            category: 'Parsing',
            msg: 'No tokens to parse',
            i: 0,
            line: 0
        }
        result.errors.push(error);
        return result;
    }

    while (p.i < p.tokens.length) {
        switch (p.tokens[p.i].kind) {

            case SpecialTokenKind.ILLEGAL:
                break;

            default:
                break;
        }
        p.i++;
    }

    return result;
}

export const parse_assignment = (p: ParsingContext): AssignNode | null => {
    return null;
}
export const parse_declaration = (p: ParsingContext): DeclareNode | null => {
    return null
}
export const parse_reassignment = (p: ParsingContext): ReassignNode | null => {
    return null
}
export const parse_expression = (p: ParsingContext): ExprNode | null => {
    return null
}
export const parse_identifier = (p: ParsingContext): IdentifierNode | null => {
    return null
}
export const parse_number = (p: ParsingContext): NumberNode | null => {
    return null
}
