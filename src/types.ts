export enum SpecialTokenKind {
    EOF = 'EOF',
    ILLEGAL = 'ILLEGAL',
    WS = 'WS',

    NUMBER = 'NUMBER',
    IDENTIFIER = 'IDENTIFIER',
}

// These tokens have a fixed string value
export enum KwdTokenKind {
    LET = 'let',
    RETURN = 'return',
}
export enum OprTokenKind {
    PLUS = '+',
    EQUALS = '==',
    ASSIGN = '=',
}
export type TokenKind = SpecialTokenKind | KwdTokenKind | OprTokenKind;

export type Token = {
    kind: TokenKind;
    value: string;
    i: number;
    line: number;
}


export enum SpecialNodeKind {
    ROOT,
    BLOCK,
}

export enum StmtNodeKind {
    DECLARE,    // 'let x' where x gets the initial value of the Type
    ASSIGN,     // 'let x = 1'
    REASSIGN,   // 'x = 2'
}

export enum ExprNodeKind {
    PLUS,
    IDENTIFIER,
    NUMBER,
}

export type RootNode = {
    kind: SpecialNodeKind.ROOT;
    statements: StmtNode[];
}

export type AssignNode = {
    kind: StmtNodeKind.ASSIGN;
    isMutable: boolean;
    isPublic: boolean;
    identifier: IdentifierNode;
    expression: ExprNode;
}
export type DeclareNode = {
    kind: StmtNodeKind.DECLARE;
    isMutable: boolean;
    isPublic: boolean;
    identifier: IdentifierNode;
}
export type ReassignNode = {
    kind: StmtNodeKind.REASSIGN;
    identifier: IdentifierNode;
    expression: ExprNode;
}


export type IdentifierNode = {
    kind: ExprNodeKind.IDENTIFIER;
    isQualified: boolean;
    name: string;
}

export type NumberNode = {
    kind: ExprNodeKind.NUMBER;
    value: number;
}

export type PlusOpNode = {
    kind: ExprNodeKind.PLUS;
    left: ExprNode;
    right: ExprNode;
}

export type StmtNode = AssignNode | DeclareNode;
export type ExprNode = PlusOpNode | IdentifierNode | NumberNode;
export type Node = StmtNode | ExprNode;

export type CompilingError = {
    category: string;
    msg: string;
    i: number;
    line: number;
}
export type LexingContext = {
    src: string;
    i: number;
    line: number;
}
export type LexingResult = {
    tokens: Token[];
    errors: CompilingError[];
}

export type ParsingContext = {
    tokens: Token[];
    i: number;
}
export type ParsingResult = {
    ast: RootNode;
    errors: CompilingError[];
}