/* now() and sleep() in generated programs. */

#include "tool_support.h"

TEST("sleep waits at least as long as asked and now never goes backwards")
{
    return run_generated(
        "fun wait(ms: Int64): Int64\n"
        "  var start: Int64 = now()\n"
        "  sleep(ms)\n"
        "  return now() - start\n"
        "end\n"
        "var a: Int64 = now()\n"
        "var b: Int64 = now()\n"
        "print(b >= a)\n"
        "var waited: Int64 = wait(50)\n"
        "print(waited >= 50, waited < 5000)\n"
        "sleep(0)\n",
        "true\ntrue true\n", "sleep and now");
}

TEST("sleep may be called from a loop")
{
    return run_generated(
        "var start: Int64 = now()\n"
        "for i in range(0, 4) do sleep(5) end\n"
        "print(now() - start >= 20)\n",
        "true\n", "sleep in a loop");
}

TEST("a negative sleep duration reports its source position")
{
    return run_generated_failure(
        "var d: Int64 = -3\nsleep(d)\n",
        MESSAGE_RUNTIME_PREFIX "test.nip:2:1 (byte 18): "
        MESSAGE_RUNTIME_SLEEP_NEGATIVE,
        "negative sleep duration");
}

TEST("an unavailable clock reports its source position")
{
    const char *driver =
        "#define _POSIX_C_SOURCE 200809L\n"
        "#include <errno.h>\n"
        "#include <time.h>\n"
        "\n"
        "int nyx_generated_main(void);\n"
        "\n"
        "int clock_gettime(clockid_t id, struct timespec *ts)\n"
        "{\n"
        "    (void)id;\n"
        "    (void)ts;\n"
        "    errno = EINVAL;\n"
        "    return -1;\n"
        "}\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    return nyx_generated_main();\n"
        "}\n";

    return run_generated_driver_failure(
        "var t: Int64 = now()\nprint(t)\n", driver,
        MESSAGE_RUNTIME_PREFIX "test.nip:1:16 (byte 15): "
        MESSAGE_RUNTIME_CLOCK_UNAVAILABLE,
        "unavailable clock");
}

TEST("a program that only reads the clock builds")
{
    /* No sleep and no Nil anywhere, so there is no Nil typedef for a sleep
       helper to name. */
    return run_generated(
        "var a: Int64 = now()\n"
        "var b: Int64 = now()\n"
        "print(b >= a)\n",
        "true\n", "now without sleep");
}
