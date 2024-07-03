import type { RootNode, BlockNode, StmtNode } from "../ast";
import type { ParsingContext } from "../defs";
import type { TranspilingError } from "../errors";
import { statement } from "./statements";

export const parse_file = (p: ParsingContext): RootNode | TranspilingError => {
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

export const block = (p: ParsingContext): BlockNode | TranspilingError => {
    let token = p.tokens[p.i];
    let stmts: StmtNode[] = [];
    while (true) {
        token = p.tokens[p.i];
        if (!token || p.i >= p.tokens.length ) break;

        let stmt = statement(p); // propagate result/error
        if (stmt) {
            if (!stmt.ok) {
                // recover error
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

