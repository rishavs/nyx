#include "harness.h"

#include <string.h>

/* One main serves both runners; the binary's own name titles the summary. */
int main(int argc, char **argv)
{
    const char *name = strrchr(argv[0], '/');

    return test_main(argc, argv, name == NULL ? argv[0] : name + 1);
}
