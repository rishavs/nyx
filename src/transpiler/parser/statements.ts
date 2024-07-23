import type { ParsingContext } from "../defs";
import { MissingSyntaxError, TranspilingError } from "../errors";
import { expression, identifier, listOfArgs } from "./expressions";
import { DeclarationNode, ExpressionNode, FunCallStmtNode, IdentifierNode, type StatementNode } from "../ast";

export const statement = (p: ParsingContext): StatementNode | TranspilingError  => {
    let token = p.tokens[p.i];

    if (token.kind == 'let') {

        p.i++;
        let id = identifier(p); // propagate result/error
        if (id instanceof TranspilingError) {
            return id;
        }

        token = p.tokens[p.i];
        if (token && token.kind === '=') {
            p.i++;
            if (! p.tokens[p.i] ) {
                return new MissingSyntaxError('Statement', token.start, token.line, token.value);
            }
            let expr = expression(p); 
            if (expr instanceof ExpressionNode) {
                return new DeclarationNode(id.at, id.line, true, id, expr);
            }
            return expr; // propagate result/error
        } else {
            return new MissingSyntaxError('Statement', token.start, token.line, token.value);
        }

    } else if (token.kind == 'IDENTIFIER') {
        let id = identifier(p); // propagate result/error
        if (id instanceof TranspilingError) {
            return id;
        }

        token = p.tokens[p.i];
        // Function call
        if (token && token.kind === '(') {
            let args = listOfArgs(p) as ExpressionNode[]; // propagate result/error
            if (args instanceof TranspilingError) {
                return args;
            }

            return new FunCallStmtNode(id.at, id.line, id, args);

        // Reasignment
        } else if (token && token.kind === '=') {
            p.i++;
            if (! p.tokens[p.i] ) {
                return new MissingSyntaxError('Statement', token.start, token.line, token.value);
            }
            let expr = expression(p); 
            if (expr instanceof ExpressionNode) {
                return new DeclarationNode(id.at, id.line, false, id, expr);
            }
            return expr; // propagate result/error

        } else {
            // temp error for now
            return new MissingSyntaxError('Statement', token.start, token.line, token.value);
        }

    } else {
        token = p.tokens[p.i];
        return new MissingSyntaxError('Statement', token.start, token.line, token.value);
    }
}