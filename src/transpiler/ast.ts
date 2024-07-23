// type BaseNode = {
//     ok: true;
//     at: number;
//     line: number;
// }

export class BaseNode {
    kind: string
    at: number;
    line: number;
    constructor(kind: string, at: number, line: number) {
        this.kind = kind;
        this.at = at;
        this.line = line;
    }
}

export class StatementNode extends BaseNode {
    constructor(kind: string, at: number, line: number) {
        super(kind, at, line);
    }
}

export class ExpressionNode extends BaseNode {
    constructor(kind: string, at: number, line: number) {
        super(kind, at, line);
    }
}

// ---------------------------
// Statements
// ---------------------------
// export type RootNode = BaseNode & {
//     kind: 'ROOT';
//     // importStmts: ImportNode[];
//     block: BlockNode;
// }

// export type BlockNode = BaseNode & {
//     kind: 'BLOCK';
//     statements: StmtNode[];
// }

// export type ReturnNode = BaseNode & {
//     kind: 'RETURN';
//     expression: ExprNode;
// }

export class RootNode extends BaseNode {
    block: BlockNode;
    constructor(at: number, line: number, block: BlockNode) {
        super('ROOT', at, line);
        this.block = block;
    }
}

export class BlockNode extends BaseNode {
    statements: StatementNode[];
    constructor(at: number, line: number, statements: StatementNode[]) {
        super('BLOCK', at, line);
        this.statements = statements;
    }
}

export class FunCallStmtNode extends StatementNode {
    id: IdentifierNode;
    expressions: ExpressionNode[];
    constructor(at: number, line: number, id: IdentifierNode, expressions: ExpressionNode[]) {
        super('FUNCALL', at, line);
        this.id = id;
        this.expressions = expressions;
    }
}

export class DeclarationNode extends StatementNode {
    isMutable?: boolean;
    isPublic?: boolean;
    isNewDeclaration: boolean;
    identifier: IdentifierNode;
    assignment: ExpressionNode | null;
    constructor(at: number, line: number, 
        isNewDeclaration: boolean, identifier: IdentifierNode, assignment?: ExpressionNode,
        isMutable?: boolean, isPublic?: boolean,
    ) {
        super('DECLARE', at, line);
        this.isMutable = isMutable;
        this.isPublic = isPublic;
        this.isNewDeclaration = isNewDeclaration;
        this.identifier = identifier;
        this.assignment = assignment || null;
    }
}


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

export class FunCallExprNode extends ExpressionNode {
    id: IdentifierNode;
    expressions: ExpressionNode[];
    constructor(at: number, line: number, id: IdentifierNode, expressions: ExpressionNode[]) {
        super('FUNCALL', at, line);
        this.id = id;
        this.expressions = expressions;
    }
}

export class IdentifierNode extends ExpressionNode {
    isQualified: boolean;
    value: string;
    constructor(at: number, line: number, isQualified: boolean, value: string) {
        super('IDENTIFIER', at, line);
        this.isQualified = isQualified;
        this.value = value;
    }
}

export class IntNode extends ExpressionNode {
    value: string;
    constructor(at: number, line: number, value: string) {
        super('INT', at, line);
        this.value = value;
    }
}

export class FloatNode extends ExpressionNode {
    value: string;
    constructor(at: number, line: number, value: string) {
        super('FLOAT', at, line);
        this.value = value;
    }
}



// export type IdentifierNode = BaseNode & {
//     kind: 'IDENTIFIER';
//     isQualified: boolean;
//     value: string;
// }

// export type IntNode = BaseNode & {
//     kind: 'INT';
//     value: string;
// }
// export type FloatNode = BaseNode & {
//     kind: 'FLOAT';
//     value: string;
// }

// export type FunCallNode = BaseNode & {
//     kind: 'FUNCALL';
//     id: IdentifierNode;
//     args: ExprNode[];
// }

// export const StatementsGroup = [FunCallNode, DeclarationNode]
// export const ExpressionsGroup = [IntNode, FloatNode, IdentifierNode, FunCallNode]

// export type ExprNodeGroup = IntNode | FloatNode | IdentifierNode | FunCallNode
// export type StmtNodeGroup = FunCallNode
 
// export type Node = StmtNode | ExprNode;
