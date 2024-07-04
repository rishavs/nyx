import type { StmtNode, FunCallNode } from "../ast";
import type { ParsingContext } from "../defs";
import { raiseUnexpectedTokenError, type TranspilingError } from "../errors";
import { identifier, listOfArgs } from "./expressions";

export const statement = (p: ParsingContext): StmtNode | TranspilingError  => {
    let token = p.tokens[p.i];

    switch (token.kind) {

        case 'IDENTIFIER':
            let id = identifier(p); // propagate result/error
            if (id && !id.ok) return id;

            token = p.tokens[p.i];
            if (token && token.kind === '(') {
                let args = listOfArgs(p); // propagate result/error
                if (args && !Array.isArray(args)) return args;
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
            return raiseUnexpectedTokenError(p, 'Statement')

        default:
            token = p.tokens[p.i];
            return raiseUnexpectedTokenError(p, 'Statement')
    }
}