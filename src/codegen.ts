import type { BlockNode, RootNode } from "./ast";

export const generate = async (ast: BlockNode) => {
    let inputFilename = 'main.nyx'

    // The abstracted filename will be used to generate the output filenames
    // the filepath relative to the project root will be used to generate the name
    let inputAbstractFilename = inputFilename.replace(/\./g, '_').toUpperCase()
    let outC99Filename = inputAbstractFilename + '.c';
    let outHeaderFilename = inputAbstractFilename + '.h';
    let outC99MainFilename = 'main.c';

    let outC99Content = /*c99*/`
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

int main() {
    return 0;
}
`;
    let outHeaderContent = /*header*/`
#include <stdio.h>

#ifndef SEAWITCH_H
#define SEAWITCH_H


#define SEAWITCH_VERSION "0.0.1"


#endif
`;  

    await Bun.write('./oven/main_nyx.c', /*c99*/`
int adder(int a, int b) {
    return a + b;
}
`);
    await Bun.write('./oven/main_nyx.h', /*header*/`
#ifndef main_nyx_h
#define main_nyx_h

int adder(int a, int b);

#endif
`);

await Bun.write('./oven/main.c', /*c99*/`
int main() {
    return 0;
}`);   
}
