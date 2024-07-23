import { RootNode, BlockNode, StatementNode } from "../ast";
import type { ParsingContext } from "../defs";
import { UnhandledError,  TranspilingError } from "../errors";
import { statement } from "./statements";

export const parse_file = (p: ParsingContext): RootNode | TranspilingError[] => {
    // Adding a try catch guard here for any unhandled errors
    let bl = block(p);
    if (bl instanceof BlockNode) {
        let rootNode = new RootNode(bl.at, bl.line, bl);
        return rootNode;
    } 
    return bl;
}

export const block = (p: ParsingContext): BlockNode | TranspilingError[] => {
    let token = p.tokens[p.i];
    let stmts: StatementNode[] = [];
    let errors: TranspilingError[] = [];

    while (p.i < p.tokens.length) {
        token = p.tokens[p.i];

        let stmt = statement(p); 
        if (stmt instanceof StatementNode) {
            stmts.push(stmt);
        // propagate result/error
        } else if (stmt instanceof TranspilingError) {
            errors.push(stmt);
            recover(p);
        } else {
            errors.push(new UnhandledError(token.start, token.line));
            recover(p);
        }
    }
    if (errors.length === 0) {
        return new BlockNode(stmts[0].at, stmts[0].line, stmts);
    }
    return errors;
}


export const recover = (p: ParsingContext) => {
    while (p.i < p.tokens.length && p.tokens[p.i].kind !== 'end') {
        p.i++;
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

