
// expression     → equality ;
// equality       → comparison ( ( "!=" | "==" ) comparison )* ;
// comparison     → term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
// term           → factor ( ( "-" | "+" ) factor )* ;
// factor         → unary ( ( "/" | "*" ) unary )* ;
// unary          → ( "!" | "-" ) unary | primary ;
// primary        → NUMBER | "(" expression ")" | functionCall ;
// functionCall   → IDENTIFIER "(" arguments? ")" ;
// arguments      → expression ( "," expression )* ;

import { ExpressionNode, FloatNode, FunCallExprNode, IdentifierNode, IntNode } from "../ast";
import type { ParsingContext } from "../defs";
import { MissingSpecificTokenError, MissingSyntaxError, TranspilingError } from "../errors";
import type { Token } from "../tokens";

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

// -----------------------------------
// Nil	nil
// Bool	true, false
// Integers	18, -12, 19_i64, 14_u32,64_u8
// Floats	1.0, 1.0_f32, 1e10, -0.5
// Char	'a', '\n', 'あ'
// String	"foo\tbar", %("あ"), %q(foo #{foo})
// Symbol	:symbol, :"foo bar"
// Array	[1, 2, 3], [1, 2, 3] of Int32, %w(one two three)
// Array-like	Set{1, 2, 3}
// Hash	{"foo" => 2}, {} of String => Int32
// Hash-like	MyType{"foo" => "bar"}
// Range	1..9, 1...10, 0..var
// Regex	/(foo)?bar/, /foo #{foo}/imx, %r(foo/)
// Tuple	{1, "hello", 'x'}
// NamedTuple	{name: "Crystal", year: 2011}, {"this is a key": 1}
// Proc	->(x : Int32, y : Int32) { x + y }
// Command	`echo foo`, %x(echo foo)

export const peekNext = (p: ParsingContext, kind: string): Token | null => {
    let token = p.tokens[p.i + 1];
    if (token && token.kind === kind) return token;
    return null;
}
export const advance = (p: ParsingContext): Token | null => {
    let token = p.tokens[p.i];
    if (token) p.i++;
    return token;
}

export const expression = (p: ParsingContext): ExpressionNode | TranspilingError => {
    return primary(p);
}



const primary = (p: ParsingContext): ExpressionNode | TranspilingError => {
    let token = p.tokens[p.i];
    switch (token.kind) {
        case 'INT':
            return int(p); // propagate result/error

        case 'FLOAT':
            return float(p); // propagate result/error

        // TODO - separate identifier from function call
        case 'IDENTIFIER':
            let id = identifier(p); // propagate result/error
            if (id instanceof TranspilingError) return id;

            token = p.tokens[p.i];
            if (token && token.kind === '(') {
                let args = listOfArgs(p) as ExpressionNode[]; // propagate result/error
                if (args instanceof TranspilingError) return args;
                return new FunCallExprNode(id.at, id.line, id, args);
            }
            return id;

        case '(':
            return grouping(p);
        
        default:
            token = p.tokens[p.i];
            return new MissingSyntaxError('Expression', token.start, token.line, token.value);
    }
}


const grouping = (p: ParsingContext): ExpressionNode | TranspilingError => {
    let token = p.tokens[p.i];
    let nextToken = p.tokens[p.i + 1];
    if (!nextToken) return new MissingSyntaxError('Grouped Expression', token.start, token.line, token.value);

    p.i++; // consume the left parenthesis

    let expr = expression(p);
    if (expr instanceof TranspilingError) return expr; // propagate error

    // If expression parsed, the next token should be a right parenthesis
    token = p.tokens[p.i];
    if (token && token.kind === ')') {
        p.i++; // consume the right parenthesis
        return expr
    }
    return new MissingSpecificTokenError ('Grouped Expression', ')', token.start, token.line, token.value);
}

export const listOfArgs = (p: ParsingContext): ExpressionNode[] | TranspilingError => {
    let token = p.tokens[p.i];
    let nextToken = p.tokens[p.i + 1];
    if (!nextToken) return new MissingSyntaxError('Function Arguments (Expressions)', token.start, token.line, token.value);
    p.i++; // consume the '('

    let args: ExpressionNode[] = [];
    if (token.kind === ')') {
        p.i++; // consume the ')'
        return args; // early return with empty list
    }

    // a leading comma is fine
    if (token.kind === ',') {
        nextToken = p.tokens[p.i + 1];
        if (!nextToken) return new MissingSyntaxError('Function Arguments (Expressions)', token.start, token.line, token.value);

        p.i++; // consume the ','
        token = p.tokens[p.i];
    }
    let expr = expression(p);
    if (expr instanceof TranspilingError) return expr; // propagate error
    // Parse the first argument
    args.push(expr);

    // Now, parse subsequent arguments if any
    while (p.tokens[p.i]) {
        token = p.tokens[p.i];
        if (token.kind === ',') {
            nextToken = p.tokens[p.i + 1];
            if (!nextToken) return new MissingSyntaxError('Function Arguments (Expressions)', token.start, token.line, token.value);

            p.i++; // Correctly consume ',' before parsing the next argument
            token = p.tokens[p.i];
            nextToken = p.tokens[p.i + 1];
            if (!nextToken) return new MissingSyntaxError('Function Arguments (Expressions)', token.start, token.line, token.value);

            // following comma is fine. if the next token is ')', then we are done
            if (token.kind === ')') {
                p.i++; // consume the ','
                break;
            }
            let expr = expression(p);
            if (expr instanceof TranspilingError) return expr; // propagate error
            args.push(expr);
        } else if (token.kind === ')') {
            p.i++; // consume the ')'
            break;
        } else {
            return new MissingSpecificTokenError('Function Arguments (Expressions)', ',', token.start, token.line, token.value);
        }
    }
    return args;
}

const int = (p: ParsingContext): IntNode => {
    let token = p.tokens[p.i];
    p.i++; // consume the number
    return new IntNode(token.start, token.line, token.value);
}

const float = (p: ParsingContext): FloatNode => {
    let token = p.tokens[p.i];
    p.i++; // consume the number
    return new FloatNode(token.start, token.line, token.value);
}

export const identifier = (p: ParsingContext): IdentifierNode | TranspilingError => {
    let cursor = p.i;
    let token = p.tokens[cursor];
    let id = new IdentifierNode(p.tokens[cursor].start, p.tokens[cursor].line, false, p.tokens[cursor].value);

    cursor++; // consume the identifier

    // handle qualified identifiers
    while (true) {
        token = p.tokens[cursor];
        if (token && token.kind === '.') {
            cursor++; // consume the '.'
            token = p.tokens[cursor];
            if (!token || token.kind !== 'IDENTIFIER') {
                return new MissingSpecificTokenError('Qualified Name', 'IDENTIFIER', token.start, token.line, token.value);
            }     
            id.isQualified = true;
            id.value += '.' + token.value;
            cursor++; // consume the identifier
        } else {
            break;
        }
    }
    p.i = cursor;
    return id;
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

//     const args: ExpressionNode[] = [];
//     if (p.tokens[p.i].kind !== ')') { // Check if there are any arguments
//         // Parse the first argument
//         let arg = expression(p);
//         if (!arg.ok) return arg; // propagate error
        
//         if (arg.result) {
//             args.push(arg.result as ExpressionNode);

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

//                 args.push(arg.result as ExpressionNode);
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

