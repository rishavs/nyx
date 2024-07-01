import type { BlockNode, ExprNode, FloatNode, FunCallNode, IntNode, RootNode, StmtNode } from "./ast";

export const gen_c99 = (ast: RootNode) => {
    let cMainFile = './oven/main.c';
    let cHeaderFile = './oven/main.h';

    let cMainContent = gen_root(ast);
    Bun.write(cMainFile, cMainContent);
}

export const gen_root = (ast: RootNode): string => {
    return /*c99*/`
int main() {
    ${gen_block(ast.block)}
    return 0;
}`
};   

export const gen_block = (block: BlockNode): string => {
    let stmt = block.statements.map(statement => `${gen_statement(statement)};`).join('\n');
    return stmt;
}

const gen_statement = (statement: StmtNode): string => {
    // Assuming you have different types of statements
    // You would check the type of the statement here and call the appropriate function
    // For example:
    switch (statement.kind) {
        case 'FUNCALL':
            return gen_funCall(statement);
        default:
            throw new Error(`Codegen Error: Unknown statement type: ${statement.kind}`);
            // return '';
    }
}

const gen_expr = (expr: ExprNode): string => {
    // Assuming ExprNode is a union type of all possible expressions
    switch (expr.kind) {
        case 'INT':
            return gen_int(expr);
        case 'FLOAT':
            return gen_float(expr);
        case 'FUNCALL':
            return gen_funCall(expr);
        // Add cases for other expression types
        default:
            throw new Error(`Codegen Error: Unknown expression type: ${expr.kind}`);
            // return '';
    }
}

const gen_funCall = (node: FunCallNode): string => {
    let id = node.id.value;
    let args = node.args.map(arg => gen_expr(arg)).join(', ');
    return `${id}(${args})`; // Ensure this line ends with a semicolon
}

const gen_int = (node: IntNode): string => {
    return node.value;
}
const gen_float = (node: FloatNode): string => {
    return node.value;
}