// Symbol interface representing a symbol entry in the symbol table
interface SymbolEntry {
    name: string;
    type: string;
    scope: string;
    references: number;
}

// SymbolTable class representing the symbol table
class SymbolsTable {
    private symbols: { [name: string]: SymbolEntry } = {};

    // Add a symbol entry to the symbol table
    public addSymbol(name: string, type: string, scope: string): void {
        if (!this.symbols[name]) {
            this.symbols[name] = {
                name,
                type,
                scope,
                references: 0,
            };
        } else {
            // Symbol already exists, handle error or update symbol if needed
            console.error(`Error: Symbol '${name}' already exists.`);
        }
    }

    // Increment the reference count for a symbol
    public incrementReferences(name: string): void {
        const symbol = this.symbols[name];
        if (symbol) {
            symbol.references++;
        } else {
            // Symbol does not exist, handle error
            console.error(`Error: Symbol '${name}' does not exist.`);
        }
    }

    // Decrement the reference count for a symbol
    public decrementReferences(name: string): void {
        const symbol = this.symbols[name];
        if (symbol) {
            if (symbol.references > 0) {
                symbol.references--;
            } else {
                // Symbol reference count is already 0, handle error
                console.error(`Error: Symbol '${name}' reference count is already 0.`);
            }
        } else {
            // Symbol does not exist, handle error
            console.error(`Error: Symbol '${name}' does not exist.`);
        }
    }

    // Retrieve symbol information from the symbol table
    public getSymbol(name: string): SymbolEntry | undefined {
        return this.symbols[name];
    }
}
