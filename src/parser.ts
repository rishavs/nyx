import { SpecialNodeKind, type AssignNode, type DeclareNode, type ReassignNode, type ExprNode, type IdentifierNode, type NumberNode } from "./ast";
import { type Token, SpecialTokenKind, KwdTokenKind } from "./tokens";
import type { ParsingContext, ParsingResult } from "./types";

// NOTE: Only expression parsing for now. And only numbers
const peakAheadOnce = (p: ParsingContext): Token | null => {
    return p.tokens[p.i + 1] || null;
}

const peakAheadTwice = (p: ParsingContext): Token | null => {
    return p.tokens[p.i + 2] || null;
}

const peakBehindOnce = (p: ParsingContext): Token | null => {
    return p.tokens[p.i - 1] || null;
}

const peakBehindTwice = (p: ParsingContext): Token | null => {
    return p.tokens[p.i - 2] || null;
}

export const parse_file = (p: ParsingContext): ParsingResult => {
    let result : ParsingResult = {
        ast: {
            kind: SpecialNodeKind.ROOT,
            statements: []
        },
        errors: []
    };
    while (p.i < p.tokens.length) {
        switch (p.tokens[p.i].kind) {
            case SpecialTokenKind.WS:
                break;

            case SpecialTokenKind.ILLEGAL:
                break;

            case KwdTokenKind.LET:
                let assign = parse_assignment(p);
                if (assign) {
                    result.ast.statements.push(assign);
                } else {
                    let declare = parse_declaration(p);
                    if (declare) {
                        result.ast.statements.push(declare);
                    }
                }

                break;

            case SpecialTokenKind.IDENTIFIER:
                // could be a reassignment for now. 
                // TODO - Later can also be function call
                let reassign = parse_reassignment(p);
                
                break;

            default:
                break;
        }
        p.i++;
    }

    return result;
}

export const parse_assignment = (p: ParsingContext): AssignNode | null => {
    return null;
}
export const parse_declaration = (p: ParsingContext): DeclareNode | null => {
    return null
}
export const parse_reassignment = (p: ParsingContext): ReassignNode | null => {
    return null
}
export const parse_expression = (p: ParsingContext): ExprNode | null => {
    return null
}
export const parse_identifier = (p: ParsingContext): IdentifierNode | null => {
    return null
}
export const parse_number = (p: ParsingContext): NumberNode | null => {
    return null
}

function parseProgram() {
    program = newProgramASTNode()
    advanceTokens()

    for (currentToken() != EOF_TOKEN) {
        statement = null
        if (currentToken() == LET_TOKEN) {
            statement = parseLetStatement()
        } else if (currentToken() == RETURN_TOKEN) {
            statement = parseReturnStatement()
        } else if (currentToken() == IF_TOKEN) {
            statement = parseIfStatement()
        }
        if (statement != null) {
            program.Statements.push(statement)
        }
        36
        advanceTokens()
    }
    return program
}

function parseLetStatement() {
    advanceTokens()
    identifier = parseIdentifier()
    advanceTokens()
    if currentToken() != EQUAL_TOKEN {
        parseError("no equal sign!")
        return null
    }
    advanceTokens()
    value = parseExpression()
    variableStatement = newVariableStatementASTNode()
    variableStatement.identifier = identifier
    variableStatement.value = value
    return variableStatement
}

function parseIdentifier() {
    identifier = newIdentifierASTNode()
    identifier.token = currentToken()
    return identifier
}
function parseExpression() {
    if (currentToken() == INTEGER_TOKEN) {
        if (nextToken() == PLUS_TOKEN) {
            return parseOperatorExpression()
        } else if (nextToken() == SEMICOLON_TOKEN) {
            return parseIntegerLiteral()
        }
    } else if (currentToken() == LEFT_PAREN) {
        return parseGroupedExpression()
    }
    // [...]
}
function parseOperatorExpression() {
    operatorExpression = newOperatorExpression()
    operatorExpression.left = parseIntegerLiteral()
    operatorExpression.operator = currentToken()
    operatorExpression.right = parseExpression()
    return operatorExpression()
}
// [...]