import type { LexingContext, ParsingContext } from "./defs";

// --------------------------------------
// All Error Definitions
// --------------------------------------
export class TranspilingError extends Error {
    at: number;
    line: number;

    constructor(msg: string, at: number, line: number) {
        super(msg);

        this.at = at;
        this.line = line;
        this.name = "Transpiling Error";
        
        // Modify the stack trace to exclude the constructor call
        if (Error.captureStackTrace) {
            Error.captureStackTrace(this, TranspilingError);
        }
    }
}

export class UnhandledError extends TranspilingError {
    constructor(at: number, line: number) {
        super('Found an unhandled error at ' + at + ' on line ' + line, at, line);
        this.cause = 'This is a bug in the compiler. Please report.'
        this.name = "Unhandled Error";

        // Modify the stack trace to exclude the constructor call
        if (Error.captureStackTrace) {
            Error.captureStackTrace(this, UnhandledError);
        }
    }
}


// export type TranspilingError = 
//     | UnhandledError
//     | IllegalTokenError
//     | UnclosedDelimiterError

// --------------------------------------
// Lexer Errors
// --------------------------------------

export class IllegalTokenError extends TranspilingError {
    constructor(token: string, at: number, line: number) {
        super(`Found illegal token "${token}", at ${line}:${at}`, at, line);
        this.name = "Syntax Error";
        
        // Modify the stack trace to exclude the constructor call
        if (Error.captureStackTrace) {
            Error.captureStackTrace(this, IllegalTokenError);
        }
    }
}
export class UnclosedDelimiterError extends TranspilingError {
    constructor(syntaxKind: string, delimiter: string, at: number, line: number) {
        
        super(`Unclosed ${syntaxKind} at ${line}:${at}`, at, line);
        this.name = "Syntax Error";
        this.cause = `Expected closing delimiter "${delimiter}" before end of input`;
        
        // Modify the stack trace to exclude the constructor call
        if (Error.captureStackTrace) {
            Error.captureStackTrace(this, UnclosedDelimiterError);
        }
    }
}

// --------------------------------------
// Parser Errors
// --------------------------------------

export class MissingSyntaxError extends TranspilingError {
    ok: boolean = false
    constructor(expectedSyntax: string, at: number, line: number, found?: string, ) {
        let expected = `Expected ${expectedSyntax} at ${line}:${at} ,`
        let got = found
            ? `but instead found "${found}"` 
            : `but instead reached end of input`
        super(expected + got, at, line);
        this.name = "Syntax Error";
        
        // Modify the stack trace to exclude the constructor call
        if (Error.captureStackTrace) {
            Error.captureStackTrace(this, MissingSyntaxError);
        }
    }
}

export class MissingSpecificTokenError extends TranspilingError {
    ok: boolean = false
    constructor(expectedSyntax: string, expectedTokenKind: string, at: number, line: number, found?: string) {
        let expected = `Expected ${expectedTokenKind} for ${expectedSyntax} at ${line}:${at},`
        let got = found
            ? `but instead found "${found}"` 
            : `but instead reached end of input`
        super(expected + got, at, line);
        this.name = "Syntax Error";
        
        // Modify the stack trace to exclude the constructor call
        if (Error.captureStackTrace) {
            Error.captureStackTrace(this, MissingSpecificTokenError);
        }
    }

}

// ArgumentErrors
// UndeclaredVariables
// RangeError
// TypeError - Missing, mismatched, wrong type

// export const raiseUnexpectedTokenError = (p: ParsingContext, expectedSyntax: string, expectedTokenKind?: string): TranspilingError =>  {
//     return {
//         ok: false,
//         cat: 'SyntaxError',
//         msg: `Expected ${expectedTokenKind ?
//             expectedTokenKind + " for " :  ""}${expectedSyntax}, but instead found "${p.tokens[p.i].kind}"`,
//         start: p.tokens[p.i].start,
//         end: p.tokens[p.i].end,
//         line: p.tokens[p.i].line
//     } as TranspilingError
// }

// export const raiseUnexpectedEndOfInput = (p: ParsingContext, expectedSyntax: string): TranspilingError => {
//     return {
//         ok: false,
//         cat: 'SyntaxError',
//         msg: `Expected ${expectedSyntax}, but instead found end of input`,
//         start: p.tokens[p.i].start,
//         end: p.tokens[p.i].end,
//         line: p.tokens[p.i].line
//     } as TranspilingError
// }

// Syntax Errors: These are mistakes in the code that violate the grammar rules of the Go language. Examples include missing brackets, semicolons (in places where they are required by Go's grammar, such as before a closing } in certain contexts), or misspelled keywords.

// Type Errors: These occur when there's a mismatch between expected and actual data types. Examples include attempting to assign a value of one type to a variable of another type without a proper conversion, or calling a function with arguments of the wrong type.

// Declaration Errors: These include redeclaring a variable in the same scope, using a variable before it is declared, or declaring a package import that is not used.

// Initialization Errors: These occur when there are issues with initializing variables. Examples include cyclic dependencies in variable initialization or using variables in their own initialization.

// Conversion and Casting Errors: Errors related to invalid attempts to convert between types, such as trying to convert a string to an integer without using a function designed for that purpose.

// Scope Errors: These occur when a variable is used outside of its scope.

// Import Errors: These include errors related to importing packages, such as importing a non-existent package, cyclic imports, or syntax errors in import statements.

// Interface Implementation Errors: These occur when a type does not properly implement an interface, such as missing methods.

// Runtime Panic Errors Detected at Compile Time: In some cases, the compiler can detect situations that will cause a runtime panic, such as indexing out of range on arrays/slices with constant indices.

// Unused Variables and Imports: The Go compiler enforces that all declared variables and imported packages are used, to promote clean code practices.

// Unreachable Code: Code that follows a return, break, continue, or goto statement, or code after an infinite loop, is considered unreachable and will cause a compile-time error.

// Mismatched Return Types: Errors related to functions not returning the expected type(s) as declared in their signature.

// Literal Overflow: This occurs when a numeric literal is too large to fit in the intended type.

// Struct Tag Syntax Errors: Errors in the syntax of struct field tags.

// Union Type Errors: Crystal's type system allows for union types (e.g., Int32 | String). Errors occur when operations are performed on union types without properly handling all possible types within the union.

// Nil Checks: Crystal requires explicit handling of nil values. The compiler catches errors where nil might be accessed without being checked first, helping to prevent nil pointer exceptions.

// Abstract Type Instantiation: Attempting to instantiate an abstract class or interface directly will result in a compile-time error.

// Generic Type Errors: Errors related to the misuse of generic types, such as providing an incorrect number of type arguments or types that do not satisfy the constraints of the generic parameters.

// Macro Errors: Crystal's macro system is powerful for metaprogramming. The compiler can catch syntax and expansion errors within macros.

// Concurrency Model Errors: Crystal uses fibers for concurrency. The compiler can catch errors related to the misuse of concurrent constructs or channels.

// Type Inference Errors: While Crystal has powerful type inference, there are cases where the compiler cannot infer the type of a variable or expression. In such cases, it requires an explicit type annotation.

// Method Overload Resolution Errors: Errors occur when the compiler cannot unambiguously determine which method overload to call based on the types and number of arguments.

// Visibility Errors: Attempting to access private or protected methods or properties from outside the class or module scope results in errors.

// Type Restriction Errors: Crystal allows specifying type restrictions on method arguments and return types. Errors occur when a given argument or return value does not match the specified type restrictions.

// Missing Method Implementation Errors: When a class includes a module or inherits from an abstract class but does not implement all required methods, the compiler will catch this.

// Constant Redefinition: Attempting to redefine a constant within the same scope will result in a compile-time error.

// Interface Implementation Errors: TypeScript enforces that classes correctly implement interfaces. Errors occur when a class does not implement all properties or methods of an interface it claims to implement.

// Access Modifier Errors: TypeScript supports access modifiers like public, private, and protected. The compiler catches errors when accessing members of a class that are not accessible due to these modifiers.

// Readonly Property Errors: Attempting to modify a property marked as readonly after its initial assignment results in a compile-time error.

// Excess Property Checks in Object Literals: When assigning an object literal to a variable of a type with known properties, TypeScript will error if the object literal contains properties that are not expected.

// Enum Errors: Errors related to misuse of enums, such as accessing an undefined enum member.

// Tuple Type Errors: TypeScript supports tuple types, and errors occur when accessing elements outside the bounds of the tuple or assigning values of incorrect types to tuple elements.

// Optional Property and Parameter Errors: Errors occur when required properties or parameters are missing or when undefined is passed to a non-optional parameter or property.

// Decorators Usage Errors: TypeScript supports experimental decorators. Errors can occur if decorators are used incorrectly, such as applying a class decorator to a method or a property.

// Namespace and Module Errors: Misuse of namespaces and modules, such as attempting to merge namespaces incorrectly or importing a module that doesn't exist, results in errors.

// Type Assertion Errors: Errors occur when asserting types in a way that TypeScript knows is not possible or when using type assertions that conflict with the known type.

// Generic Constraints Errors: When using generics, TypeScript enforces constraints on type parameters. Errors occur if a type does not satisfy the constraints of the generic it is being used with.

// Ambient Declaration Errors: Errors related to incorrect or conflicting ambient declarations, which are used to describe the shape of already existing libraries or JavaScript code.

// Non-null Assertion Errors: TypeScript allows non-null assertions using the ! postfix operator. Misuse or incorrect use of this operator can lead to errors, especially if the --strictNullChecks flag is enabled.

// Type Compatibility and Assignment Errors: TypeScript's structural type system checks that the shape of an object is compatible with the expected type. Errors occur when trying to assign a value of a type to a variable of another type where the source type does not match the target type's structure.