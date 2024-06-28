type BaseNode = {
    ok: true;
    start: number;
    end: number;
    line: number;
}

// ---------------------------
// Statements
// ---------------------------
export type RootNode = BaseNode & {
    kind: 'ROOT';
    statements: StmtNode[];
}

export type BlockNode = BaseNode & {
    kind: 'BLOCK';
    statements: StmtNode[];
}

export type ReturnNode = BaseNode & {
    kind: 'RETURN';
    expression: ExprNode;
}


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


export type IdentifierNode = BaseNode & {
    kind: 'IDENTIFIER';
    isQualified: boolean;
    value: string;
}

export type IntNode = BaseNode & {
    kind: 'INT';
    value: string;
}
export type FloatNode = BaseNode & {
    kind: 'FLOAT';
    value: string;
}

export type FunCallNode = BaseNode & {
    kind: 'FUNCALL';
    id: IdentifierNode;
    args: ExprNode[];
}


export type ExprNode = IntNode | FloatNode | IdentifierNode | FunCallNode
export type StmtNode = FunCallNode
 
// export type Node = StmtNode | ExprNode;
