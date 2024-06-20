export enum SpecialNodeKind {
    ROOT,
    BLOCK,
}

// ---------------------------
// Statements
// ---------------------------

export enum StmtNodeKind {
    DECLARE,    // 'let x' where x gets the initial value of the Type
    ASSIGN,     // 'let x = 1'
    REASSIGN,   // 'x = 2'
}


// export type RootNode = {
//     kind: SpecialNodeKind.ROOT;
//     statements: StmtNode[];
// }

// export type AssignNode = {
//     kind: StmtNodeKind.ASSIGN;
//     isMutable: boolean;
//     isPublic: boolean;
//     identifier: IdentifierNode;
//     expression: ExprNode;
// }
// export type DeclareNode = {
//     kind: StmtNodeKind.DECLARE;
//     isMutable: boolean;
//     isPublic: boolean;
//     identifier: IdentifierNode;
// }
// export type ReassignNode = {
//     kind: StmtNodeKind.REASSIGN;
//     identifier: IdentifierNode;
//     expression: ExprNode;
// }

// export type StmtNode = AssignNode | DeclareNode;

// ---------------------------
// Expressions
// ---------------------------
// expression     → literal
//                | unary
//                | binary
//                | grouping ;

// literal        → NUMBER | STRING | "true" | "false" | "nil" ;
// grouping       → "(" expression ")" ;
// unary          → ( "-" | "!" ) expression ;
// binary         → expression operator expression ;
// operator       → "==" | "!=" | "<" | "<=" | ">" | ">="
//                | "+"  | "-"  | "*" | "/" | "and" | "or";

export enum ExprNodeKind {
    IDENTIFIER,
    NUMBER,
    NIL,

    UNARY,
    BINARY,

    GROUPING,
}

export type IdentifierNode = {
    kind: ExprNodeKind.IDENTIFIER;
    isQualified: boolean;
    name: string;
}

export type NilNode = {
    kind: ExprNodeKind.NIL;
}

export type NumberNode = {
    kind: ExprNodeKind.NUMBER;
    value: string;
}

export type UnaryNode = {
    kind: ExprNodeKind.UNARY;
    operator: string;
    right: ExprNode;
}

export type BinaryNode = {
    kind: ExprNodeKind.BINARY;
    left: ExprNode;
    operator: string;
    right: ExprNode;
}

export type GroupingNode = {
    kind: ExprNodeKind.GROUPING;
    expression: ExprNode;
}

export type ExprNode = 
    IdentifierNode 
    | NumberNode 
    | NilNode
    | UnaryNode 
    | BinaryNode 
    | GroupingNode;

// export type Node = StmtNode | ExprNode;
