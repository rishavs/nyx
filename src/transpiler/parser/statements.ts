import type { StmtNode, FunCallNode, ExprNode } from "../ast";
import type { ParsingContext } from "../defs";
import { MissingSyntaxError, type TranspilingError } from "../errors";
import { identifier, listOfArgs } from "./expressions";

export const statement = (p: ParsingContext): StmtNode | TranspilingError  => {
    let token = p.tokens[p.i];

    switch (token.kind) {

        case 'IDENTIFIER':
            let id = identifier(p); // propagate result/error

            token = p.tokens[p.i];
            if (token && token.kind === '(') {
                let args = listOfArgs(p) as ExprNode[]; // propagate result/error
                return {
                    ok: true,
                    kind: 'FUNCALL',
                    id: id,
                    args: args,
                    start: id.start,
                    end: args[args.length - 1].end,
                    line: id.line
                } as FunCallNode
            }
            throw new MissingSyntaxError(p, 'Function Call Statement')

        default:
            token = p.tokens[p.i];
            throw new MissingSyntaxError(p, 'Statement')
    }
}