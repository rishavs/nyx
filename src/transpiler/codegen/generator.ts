import type { BlockNode, ExprNode, FloatNode, FunCallNode, IntNode, RootNode, StmtNode } from "../ast";

export const gen_c99 = async (ast: RootNode) => {
    let cMainFile = './oven/main.c';
    let cHeaderFile = './oven/main.h';

    let cMainContent = gen_root(ast);
    console.log("Generated C99 code: \n", cMainContent);
    let mainBytes = await Bun.write(cMainFile, cMainContent);
    let headerBytes = await Bun.write (cHeaderFile, `#include <stdio.h>\n`);
    console.log(`Wrote ${mainBytes} bytes to ${cMainFile}`);
    console.log(`Wrote ${headerBytes} bytes to ${cHeaderFile}`);
}

export const gen_root = (ast: RootNode): string => {
    return /*c99*/`
int main() {
${gen_block(ast.block, 1)}
    return 0;
}`
};

export const gen_block = (block: BlockNode, indentLevel: number = 0): string => {
    let indent = '    '.repeat(indentLevel); // Using 4 spaces for indentation
    let stmts = block.statements.map(statement => `${indent}${gen_statement(statement, indentLevel)};`).join('\n');
    return stmts;
}

const gen_statement = (statement: StmtNode, indentLevel: number): string => {
    // Assuming you have different types of statements
    // You would check the type of the statement here and call the appropriate function
    // For example:
    switch (statement.kind) {
        case 'FUNCALL':
            return gen_funCall(statement, indentLevel);
        default:
            throw new Error(`Codegen Error: Unknown statement type: ${statement.kind}`);
            // return '';
    }
}

const gen_expr = (expr: ExprNode, indentLevel: number): string => {
    // Assuming ExprNode is a union type of all possible expressions
    switch (expr.kind) {
        case 'INT':
            return gen_int(expr);
        case 'FLOAT':
            return gen_float(expr);
        case 'FUNCALL':
            return gen_funCall(expr, indentLevel);
        // Add cases for other expression types
        default:
            throw new Error(`Codegen Error: Unknown expression type: ${expr.kind}`);
            // return '';
    }
}

const gen_funCall = (node: FunCallNode, indentLevel: number): string => {
    let id = node.id.value;
    let args = node.args.map(arg => gen_expr(arg, indentLevel)).join(', ');
    return `${id}(${args})`; // Ensure this line ends with a semicolon
}

const gen_int = (node: IntNode): string => {
    return node.value.toString();
}

const gen_float = (node: FloatNode): string => {
    return node.value.toString();
}