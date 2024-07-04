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

export const block = (p: ParsingContext): BlockNode | TranspilingError[] => {
    let token = p.tokens[p.i];
    let stmts: StmtNode[] = [];
    let errors: TranspilingError[] = [];

    while (true) {
        token = p.tokens[p.i];
        if (!token || p.i >= p.tokens.length ) break;

        try {
            let stmt = statement(p); // propagate result/error
            if (stmt) {
                if (!stmt.ok) {
                    // recover error
                    errors.push(stmt);
                } else {
                    stmts.push(stmt);
                }
            } else {
                break;
            }
        } catch (error) {
            console.error(error); // Print the error
            if (error instanceof Error) {
                errors.push({
                    ok: false,
                    cat: 'SyntaxError',
                    msg: error.message,
                    start: p.tokens[p.i].start,
                    end: p.tokens[p.i].end,
                    line: p.tokens[p.i].line
                });
            }

            // Recover from error
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

