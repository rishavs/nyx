import { type ExprNode, type IdentifierNode, type IntNode, type FloatNode, type FunCallNode } from './ast';
import { type ParsingContext, type UnexpectedSyntax } from './types';

// expression     → equality ;
// equality       → comparison ( ( "!=" | "==" ) comparison )* ;
// comparison     → term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
// term           → factor ( ( "-" | "+" ) factor )* ;
// factor         → unary ( ( "/" | "*" ) unary )* ;
// unary          → ( "!" | "-" ) unary | primary ;
// primary        → NUMBER | "(" expression ")" | functionCall ;
// functionCall   → IDENTIFIER "(" arguments? ")" ;
// arguments      → expression ( "," expression )* ;

//  -----------------------------------
// expression     → assignment ;

// assignment     → ( call "." )? IDENTIFIER "=" assignment
//                | logicOr;

// logicOr       → logicAnd ( "or" logicAnd )* ;
// logicAnd      → equality ( "and" equality )* ;
// equality       → comparison ( ( "!=" | "==" ) comparison )* ;
// comparison     → addition ( ( ">" | ">=" | "<" | "<=" ) addition )* ;
// addition       → multiplication ( ( "-" | "+" ) multiplication )* ;
// multiplication → unary ( ( "/" | "*" ) unary )* ;

// unary          → ( "!" | "-" ) unary | call ;
// call           → primary (
//                    ( "[" genericArgs "]" )? "(" expressionList? ")"
//                    | "." IDENTIFIER
//                  )* ;
// primary        → "true" | "false" | NUMBER | "(" expression ")"

export const failedExpectation = (p: ParsingContext, expectedSyntax: string, expectedTokenKind?: string): UnexpectedSyntax =>  {
    return {
        ok: false,
        expectedSyntax,
        expectedTokenKind,
        got: p.tokens[p.i]
    } as UnexpectedSyntax
}

export const expression = (p: ParsingContext): ExprNode | UnexpectedSyntax => {
    return primary(p);
}


const int = (p: ParsingContext): IntNode => {
    let token = p.tokens[p.i];
    p.i++; // consume the number
    return {
            ok: true,
            kind: 'INT',
            value: token.value,
            start: token.start,
            end: token.end,
            line: token.line
        } as IntNode
}

const float = (p: ParsingContext): FloatNode => {
    let token = p.tokens[p.i];
    p.i++; // consume the number
    return {
        ok: true,
        kind: 'FLOAT',
        value: token.value,
        start: token.start,
        end: token.end,
        line: token.line
    } as FloatNode
}


export const identifier = (p: ParsingContext): IdentifierNode | UnexpectedSyntax => {
    let cursor = p.i;
    let token = p.tokens[cursor];
    let id = {
        ok: true,
        kind: 'IDENTIFIER',
        isQualified: false,
        value: token.value,
        start: token.start,
        end: token.end,
        line: token.line,
    } as IdentifierNode

    cursor++; // consume the identifier

    // handle qualified identifiers
    while (true) {
        token = p.tokens[cursor];
        if (token && token.kind === '.') {
            cursor++; // consume the '.'
            token = p.tokens[cursor];
            if (!token || token.kind !== 'IDENTIFIER') {
                return failedExpectation(p, 'Qualified Name', 'IDENTIFIER')
            }     
            id.isQualified = true;
            id.value += '.' + token.value;
            id.end = token.end;
            cursor++; // consume the identifier
        } else {
            id.end = token.end - 1;
            break;
        }
    }
    p.i = cursor;
    return id;
}

const grouping = (p: ParsingContext): ExprNode | UnexpectedSyntax => {
    p.i++; // consume the left parenthesis
    let token = p.tokens[p.i];

    let expr = expression(p);
    if (!expr) return failedExpectation(p, 'Expression');
    if (expr && !expr.ok) return expr; // propagate error
    

    // If expression parsed, the next token should be a right parenthesis
    token = p.tokens[p.i];
    if (token && token.kind === ')') {
        p.i++; // consume the right parenthesis
        return expr
    }
    return failedExpectation(p, 'Grouped Expression', ')');
}


export const listOfArgs = (p: ParsingContext): ExprNode[] | UnexpectedSyntax => {
    p.i++; // consume the '('
    let token = p.tokens[p.i];
    if (!token) return failedExpectation(p, 'Argument or Expression');

    let args: ExprNode[] = [];
    if (token.kind === ')') {
        p.i++; // consume the ')'
        return args; // early return with empty list
    }

    // a leading comma is fine
    if (token.kind === ',') {
        p.i++; // consume the ','
        token = p.tokens[p.i];
        if (!token) return failedExpectation(p, 'Argument or Expression');
    }

    // Parse the first argument
    let arg = expression(p);
    if (!arg) return failedExpectation(p, 'Argument');
    if (arg && !arg.ok) return arg; // propagate error
    args.push(arg);

    // Now, parse subsequent arguments if any
    while (true) {
        token = p.tokens[p.i];
        if (!token) return failedExpectation(p, 'Argument or Expression');
        if (token.kind === ',') {
            p.i++; // Correctly consume ',' before parsing the next argument
            token = p.tokens[p.i];
            if (!token) return failedExpectation(p, 'Argument or Expression');

            // following comma is fine. if the next token is ')', then we are done
            if (token.kind === ')') {
                p.i++; // consume the ','
                break;
            }
            arg = expression(p);
            if (!arg) return failedExpectation(p, 'Argument');
            if (arg && !arg.ok) return arg; // propagate error
            args.push(arg);
        } else if (token.kind === ')') {
            p.i++; // consume the ')'
            break;
        } else {
            return failedExpectation(p, 'Argument or Expression');
        }
    }
    return args;
}

// const functionCall = (p: ParsingContext): ExprParsingResult => {
//     let token = p.tokens[p.i];

//     const functionName = token.value;
//     p.i++; // consume the identifier

//     // If there is no '(' after the identifier, then it is not a function call
//     // instead it is just an identifier
//     token = p.tokens[p.i];
//     if (!token || token.kind !== '(') {
//         return {
//             ok: true,
//             result: {
//                 kind: 'IDENTIFIER',
//                 isQualified: false,
//                 value: functionName,
//                 i: token.i,
//                 line: token.line,
//             } as IdentifierNode
//         }
//     }
//     p.i++; // consume the '('

//     const args: ExprNode[] = [];
//     if (p.tokens[p.i].kind !== ')') { // Check if there are any arguments
//         // Parse the first argument
//         let arg = expression(p);
//         if (!arg.ok) return arg; // propagate error
        
//         if (arg.result) {
//             args.push(arg.result as ExprNode);

//             // Now, parse subsequent arguments if any
//             while (p.tokens[p.i] && p.tokens[p.i].kind === ',') {
//                 p.i++; // Correctly consume ',' before parsing the next argument
//                 arg = expression(p);
//                 if (!arg.ok) return arg; // propagate error

//                 if (!arg.result) {
//                     return {
//                         ok: false,
//                         result: {
//                             category: 'Parsing',
//                             msg: `Expected an argument after the comma, but found none`,
//                             i: token.i,
//                             line: token.line,
//                         } as CompilingError
//                     }
//                 }

//                 args.push(arg.result as ExprNode);
//             }
//         }
//     }

//     token = p.tokens[p.i];
//     if (!token || token.kind !== ')') {
//         return {
//             ok: false,
//             result: {
//                 category: 'Parsing',
//                 msg: `Expected ')', found ${token.kind}`,
//                 i: token.i,
//                 line: token.line,
//             } as CompilingError
//         }
//     }
//     p.i++; // consume the ')'

//     let idNode = {
//         kind: 'IDENTIFIER',
//         isQualified: false,
//         value: functionName,
//         i: token.i,
//         line: token.line,
//     } as IdentifierNode;

//     let funcallNode = {
//         kind: 'FUNCALL',
//         id: idNode,
//         args: args,
//         i: token.i,
//         line: token.line,
//     } as FunCallNode;

//     return {
//         ok: true,
//         result: funcallNode
//     }
// }


// const equality = (p: ParsingContext): ExprParsingResult => {
//     let left = comparison(p);
//     if ('category' in left) return left; // propagate error

//     let token = p.tokens[p.i];
//     while (token && (token.kind === '!=' || token.kind === '==')) {
//         p.i++; // consume the operator
//         let right = comparison(p);
//         if ('category' in right) return right; // propagate error

//         left = {
//             kind: 'BINARY',
//             left: left,
//             operator: token.kind,
//             right: right,
//             i: token.i,
//             line: token.line,
//         } as BinaryNode;
//         token = p.tokens[p.i];
//     }
//     return left;
// }

// const comparison = (p: ParsingContext): ExprParsingResult => {
//     let left = term(p);
//     if ('category' in left) return left; // propagate error

//     let token = p.tokens[p.i];
//     while (token && (token.kind === '>' || token.kind === '>=' || token.kind === '<' || token.kind === '<=')) {
//         p.i++; // consume the operator
//         let right = term(p);
//         if ('category' in right) return right; // propagate error

//         left = {
//             kind: 'BINARY',
//             left: left,
//             operator: token.kind,
//             right: right,
//             i: token.i,
//             line: token.line,
//         } as BinaryNode;
//         token = p.tokens[p.i];
//     }
//     return left;
// }

// const term = (p: ParsingContext): ExprParsingResult => {
//     let left = factor(p);
//     if ('category' in left) return left; // propagate error

//     let token = p.tokens[p.i];
//     while (token && (token.kind === '+' || token.kind === '-')) {
//         p.i++; // consume the operator
//         let right = factor(p);
//         if ('category' in right) return right; // propagate error

//         left = {
//             kind: 'BINARY',
//             left: left,
//             operator: token.kind,
//             right: right,
//             i: token.i,
//             line: token.line,
//         } as BinaryNode;
//         token = p.tokens[p.i];
//     }
//     return left;
// }

// const factor = (p: ParsingContext): ExprParsingResult => {
//     let left = unary(p);
//     if ('category' in left) return left; // propagate error

//     let token = p.tokens[p.i];
//     while (token && (token.kind === '*' || token.kind === '/')) {
//         p.i++; // consume the operator
//         let right = unary(p);
//         if ('category' in right) return right; // propagate error

//         left = {
//             kind: 'BINARY',
//             left: left,
//             operator: token.kind,
//             right: right,
//             i: token.i,
//             line: token.line,
//         } as BinaryNode;
//         token = p.tokens[p.i];
//     }
//     return left;
// }

// const unary = (p: ParsingContext): ExprParsingResult => {
//     let token = p.tokens[p.i];
//     if (token.kind === '!' || token.kind === '-') {
//         p.i++; // consume the operator
//         let right = unary(p);
//         if ('category' in right) return right; // propagate error
//         return {
//             kind: 'UNARY',
//             operator: token.kind,
//             right: right,
//             i: token.i,
//             line: token.line,
//         } as UnaryNode;
//     }
//     return primary(p);
// }



const primary = (p: ParsingContext): ExprNode | UnexpectedSyntax => {
    let token = p.tokens[p.i];
    switch (token.kind) {
        case 'INT':
            return int(p); // propagate result/error

        case 'FLOAT':
            return float(p); // propagate result/error

        // TODO - separate identifier from function call
        case 'IDENTIFIER':
            let id = identifier(p); // propagate result/error
            if (id && !id.ok) return id;

            token = p.tokens[p.i];
            if (token && token.kind === '(') {
                let args = listOfArgs(p); // propagate result/error
                if (args && !Array.isArray(args)) return args;
                return {
                    ok: true,
                    kind: 'FUNCALL',
                    id: id,
                    args: args,
                    start: id.start,
                    end: args[args.length - 1].end,
                    line: id.line
                } as FunCallNode
            }
            return id;

        case '(':
            return grouping(p);
        
        default:
            token = p.tokens[p.i];
            return failedExpectation(p, 'Expression')
    }
}
