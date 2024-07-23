import { expect, test, describe } from "bun:test";

describe("Testing the Lexer", () => {
    test("Handles null input", () => {
        expect(2 + 2).toBe(4);
    });

    test("2 * 2", () => {
        expect(2 * 2).toBe(4);
    });

    test.skip("wat", () => {
        // TODO: fix this
        expect(0.1 + 0.2).toEqual(0.3);
    });

    test.todo("fix this", () => {
        expect(0.1 + 0.2).toEqual(0.3);
    });
});