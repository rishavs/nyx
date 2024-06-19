import { ExprNodeKind, KwdTokenKind, OprTokenKind, SpecialNodeKind, SpecialTokenKind, StmtNodeKind, type AssignNode, type DeclareNode, type ExprNode, type IdentifierNode, type NumberNode, type ParsingContext, type ParsingResult, type ReassignNode, type RootNode, type StmtNode, type Token } from "./types";

export const parse_file = (p: ParsingContext): ParsingResult => {
    let result : ParsingResult = {
        ast: {
            kind: SpecialNodeKind.ROOT,
            statements: []
        },
        errors: []
    };
    while (p.i < p.tokens.length) {
        switch (p.tokens[p.i].kind) {
            case SpecialTokenKind.WS:
                break;

            case SpecialTokenKind.ILLEGAL:
                break;

            case KwdTokenKind.LET:
                let assign = parse_assignment(p);
                if (assign) {
                    result.ast.statements.push(assign);
                } else {
                    let declare = parse_declaration(p);
                    if (declare) {
                        result.ast.statements.push(declare);
                    }
                }

                break;

            case SpecialTokenKind.IDENTIFIER:
                // could be a reassignment for now. 
                // TODO - Later can also be function call
                let reassign = parse_reassignment(p);
                
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
