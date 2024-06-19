
const seq = (name: string, parsers: Parser[]): Parser => {
    return (p: ParsingState): ParsingResult => {
        let start = p.i;
        let children: Node[] = [];
        let errors: ParsingError[] = [];
        for (let parser of parsers) {
            let currentNode = { ...p.currentNode };
            let result = parser(p);
            if (!result.ok) {
                errors.push(result.result as ParsingError);
                p.currentNode = currentNode as Node;
                return { ok: false, result: errors[0] };
            }
            children.push(result.result as Node);
        }
        let node: Node = {
            type: name,
            value: p.src.slice(start, p.i),
            i: start,
            line: p.line,
            parent: p.currentNode,
            children: children,
        };
        p.currentNode.children.push(node);
        return { ok: true, result: node };
    };
};
