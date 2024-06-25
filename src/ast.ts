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
    RETURN,     // 'return x'
}


export type RootNode = {
    kind: SpecialNodeKind.ROOT;
    statements: StmtNode[];
}

export type BlockNode = {
    kind: SpecialNodeKind.BLOCK;
    statements: StmtNode[];
}

export type ReturnNode = {
    kind: StmtNodeKind.RETURN;
    expression: ExprNode;
}

export type StmtNode = ReturnNode;

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
// expression     → equality ;
// equality       → comparison ( ( "!=" | "==" ) comparison )* ;
// comparison     → term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
// term           → factor ( ( "-" | "+" ) factor )* ;
// factor         → unary ( ( "/" | "*" ) unary )* ;
// unary          → ( "!" | "-" ) unary
//                | primary ;
// primary        → NUMBER | STRING | "true" | "false" | "nil" | "(" expression ")" ;

export type ExprNodeKind =
    'IDENTIFIER'
    | 'NUMBER'
    | 'STRING'
    | 'BOOLEAN'
    | 'NIL'
    | 'UNARY'
    | 'BINARY'
    | 'GROUPING';

type BaseNode = {
    value: string;
    i: number;
    line: number;
}

export type IdentifierNode = BaseNode & {
    kind: 'IDENTIFIER';
    isQualified: boolean;
}

export type NumberNode = BaseNode & {
    kind: 'NUMBER';
}

export type StringNode = BaseNode & {
    kind: 'STRING';
    isStatic: boolean;
    isInterpolated: boolean;
}

export type BooleanNode = BaseNode & {
    kind: 'BOOLEAN';
}

export type NilNode = BaseNode & {
    kind: 'NIL';
}

export type FunCallNode = BaseNode & {
    kind: 'FUNCALL';
    id: IdentifierNode;
    args: ExprNode[];
}

export type UnaryNode = BaseNode & {
    kind: 'UNARY';
    operator: string;
    right: ExprNode;
}

export type BinaryNode = BaseNode & {
    kind: 'BINARY';
    left: ExprNode;
    operator: string;
    right: ExprNode;
}

export type GroupingNode = BaseNode & {
    kind: 'GROUPING';
    expression: ExprNode;
}

export type ExprNode = 
    IdentifierNode 
    | NumberNode 
    | StringNode
    | BooleanNode
    | NilNode
    | FunCallNode
    | UnaryNode 
    | BinaryNode 
    | GroupingNode;

// export type Node = StmtNode | ExprNode;
