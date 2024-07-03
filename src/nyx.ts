import { transpile_file } from "./transpiler/transpiler";

let args = Bun.argv.slice(2);
if (args.length === 0) {
    console.log("Seawitch Programming Language");
    console.log("Version: 0.0.1");
    console.log("Usage: seawitch [options] <file>");
    console.log("Options:");
    console.log("  -h, --help\t\tPrint this help message");
    console.log("  -v, --version\t\tPrint version information");
} else if (args.length === 2 && args[0] === "run") {   
    let filepath = args[1];
    // check if file exists
    const file = Bun.file(filepath);
    if (! await file.exists()) {
        console.log("Error: File not found");
    }
    // Check if file is a seawitch file *.sea
    if (!filepath.endsWith(".sea")) {
        console.log("Error: Invalid file type. Seawitch files must have a .sea extension");
    }

    console.log("Compiling file: " + filepath);
    
    // Transpile file
    let tres = transpile_file(await file.text());
    process.exit(tres ? 0 : 1);

    // Compile file
    // Execute file

} else {
    console.log("Invalid arguments");
}

// #include <stdio.h>
// #include <string.h>
// #include <time.h>

// #include "../include/seawitch.h"
// #include "../include/compiler.h"


// int main(int argc, char* argv[]) {

//     if (argc == 1) {
//         printf("Seawitch Programming Language\n");
//         printf("Version: %s\n", SEAWITCH_VERSION);
//         printf("Usage: hexal [options] <file>\n");
//         printf("Options:\n");
//         printf("  -h, --help\t\tPrint this help message\n");
//         printf("  -v, --version\t\tPrint version information\n");
//         return 0;
//     } else if (argc == 3 && strcmp(argv[1], "run") == 0) {
//         printf("Compiling project with entrypoint file: %s\n", argv[2]);
//         char* filepath = argv[2];
        
//         int compiler_res = compile_file( filepath);
//         return compiler_res;
        
//     } else {
//         printf("Invalid arguments\n");
//         return 1;
//     }

//     return 0;
// }

