import type { BlockNode, ExprNode, FunCallNode, RootNode, StmtNode } from "./ast";
import { identifier, listOfArgs, failedExpectation } from "./expressions";
import type { ParsingContext, UnexpectedSyntax } from "./types";

export const root = (p: ParsingContext): RootNode | UnexpectedSyntax => {
    let bl = block(p);
    if (!bl.ok) return bl;
    return {
        ok: true,
        kind: 'ROOT',
        block: bl,
        start: bl.start,
        end: bl.end,
        line: bl.line
    }
}

export const block = (p: ParsingContext): BlockNode | UnexpectedSyntax => {
    let token = p.tokens[p.i];
    let stmts: StmtNode[] = [];
    while (true) {
        token = p.tokens[p.i];
        if (!token || p.i >= p.tokens.length ) break;

        let stmt = statement(p); // propagate result/error
        if (stmt) {
            if (!stmt.ok) {
                return stmt;
            } else {
                stmts.push(stmt);
            }
        } else {
            break;
        }
    }
    return {
        ok: true,
        kind: 'BLOCK',
        statements: stmts,
        start: stmts[0].start,
        end: stmts[stmts.length - 1].end,
        line: stmts[0].line
    }
}

export const statement = (p: ParsingContext): StmtNode | UnexpectedSyntax  => {
    let token = p.tokens[p.i];
    console.log('statement', token)
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
            return failedExpectation(p, 'Statement')

        default:
            token = p.tokens[p.i];
            return failedExpectation(p, 'Statement')
    }
}
// program        → declaration* EOF ;


// declaration    → classDecl
//                | funDecl
//                | varDecl
//                | typeDecl
//                | statement ;

// classDecl      → "class" IDENTIFIER ( "[" genericParams "]" )?
//                  ( "<" genericType )? "{" ( function | field )* "}" ;
// funDecl        → "fun" function ;
// typeDecl       → "type" IDENTIFIER ( "[" genericParams "]" )? "=" typeExpr ;
// varDecl        → "var" IDENTIFIER (":" typeExpr)? ( "=" expression )? ";" ;


// statement      → exprStmt
//                | forStmt
//                | ifStmt
//                | printStmt
//                | returnStmt
//                | whileStmt
//                | block ;

// exprStmt       → expression ";" ;
// forStmt        → "for" "(" ( varDecl | exprStmt | ";" )
//                            expression? ";"
//                            expression? ")" statement ;
// ifStmt         → "if" "(" expression ")" statement ( "else" statement )? ;
// printStmt      → "print" expression ";" ;
// returnStmt     → "return" expression? ";" ;
// whileStmt      → "while" "(" expression ")" statement ;
// block          → "{" declaration* "}" ;

// -----------------------------------
// program        → declaration* EOF ;


// declaration    → classDecl
//                | funDecl
//                | varDecl
//                | typeDecl
//                | statement ;

// classDecl      → "class" IDENTIFIER ( "[" genericParams "]" )?
//                  ( "<" genericType )? "{" ( function | field )* "}" ;
// funDecl        → "fun" function ;
// typeDecl       → "type" IDENTIFIER ( "[" genericParams "]" )? "=" typeExpr ;
// varDecl        → "var" IDENTIFIER (":" typeExpr)? ( "=" expression )? ";" ;


// statement      → exprStmt
//                | forStmt
//                | ifStmt
//                | printStmt
//                | returnStmt
//                | whileStmt
//                | block ;

// exprStmt       → expression ";" ;
// forStmt        → "for" "(" ( varDecl | exprStmt | ";" )
//                            expression? ";"
//                            expression? ")" statement ;
// ifStmt         → "if" "(" expression ")" statement ( "else" statement )? ;
// printStmt      → "print" expression ";" ;
// returnStmt     → "return" expression? ";" ;
// whileStmt      → "while" "(" expression ")" statement ;
// block          → "{" declaration* "}" ;

