import type { Token } from "./tokens";
import type { CompilingError, ParsingContext } from "./types";

type BaseNode = {
    ok:true;
    start: number;
    end: number;
    line: number;
}

type IdentifierNode = BaseNode & {
    kind: 'IDENTIFIER';
    isQualified: boolean;
    value: string;
}

type IntNode = BaseNode & {
    kind: 'INT';
    value: string;
}
type FloatNode = BaseNode & {
    kind: 'FLOAT';
    value: string;
}

type FunCallNode = BaseNode & {
    kind: 'FUNCALL';
    id: IdentifierNode;
    args: ExprNode[];
}

type RootNode = BaseNode & {
    kind: 'ROOT';
    statements: StmtNode[];
}
type ExprNode = IntNode | FloatNode | IdentifierNode | FunCallNode
type StmtNode = FunCallNode

type Context = {
    src: string;
    i: number;
    errors: CompilingError[];
    ast: RootNode;
}

type SyntaxError = {
    expected: string;
    expectedTokenKind: string;
    got: Token;
}

class Parser {
    tokens: Token[];
    i: number;
    line: number;
    errors: CompilingError[];
    ast: RootNode;

    constructor(tokens: Token[]) {
        this.tokens = tokens;
        this.i = 0;
        this.line = 1;
        this.errors = [];
        this.ast = {
            kind: 'ROOT',
            ok: true,
            start: 0,
            end: 0,
            line: 1,
            statements: [],
        };
    }

    // Parses numbers
    int = (): IntNode | boolean => {
        let token = this.tokens[this.i];
        if (!token || token.kind !== 'INT') {
            return false;
        }
        let int = {
            kind: 'INT',
            ok: true,
            start: token.start,
            end: token.end,
            line: token.line,
            value: token.value,
        } as IntNode;

        this.i++;
        return int;
    };

    // Parses identifiers. handle qualified identifiers 
    id = (): IdentifierNode | boolean => {
        let token = this.tokens[this.i];
        if (!token || token.kind !== 'IDENTIFIER') {
            return false;
        }

        let id = {
            kind: 'IDENTIFIER',
            ok: true,
            start: token.start,
            end: token.end,
            line: token.line,
            value: token.value,
            isQualified: false,
        } as IdentifierNode;

        this.i++;
        return id;
    };

    is = (kind: string): boolean => {
        let token = this.tokens[this.i];
        return token && token.kind === kind;
    }

    // Parses a sequence of parsers
    seq = (...parsers) => {
        let start = this.i;
        let results = [];
        for (let parser of parsers) {
            let result = parser();
            if (result === false) {
                this.i = start;
                return false;
            }
            results.push(result);
        }
        let end = this.i;
        return results;
    }

    // Main run function to start parsing
    run = (): RootNode | SyntaxError => {
        let qid = this.seq (this.id, this.is(`.`), this.id())
    };
}

const parser = new Parser('xxx');
console.log(JSON.stringify(parser.run(), null, 2));