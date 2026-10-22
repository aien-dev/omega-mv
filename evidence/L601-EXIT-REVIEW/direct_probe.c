/* Existing host builtin included unchanged; unused builtins are linker-discarded. */
#ifndef L601_BUILTIN
#define L601_BUILTIN "osh_builtin.c"
#endif
#include L601_BUILTIN
_Static_assert(sizeof(long) == 8, "Linux LP64 scope");
int main(int argc, char **argv)
{
    if (argc < 3) return 64;
    OshSession s = {0};
    s.last_status = atoi(argv[2]);
    s.exit_status = 93;
    OshCmd c = {0};
    c.nargv = argc - 2;
    c.argv[0] = "exit";
    for (int i = 3; i < argc; i++) c.argv[i - 2] = argv[i];
    const int io[3] = {0, 1, 2};
    int result = bi_exit(&s, &c, io, atoi(argv[1]));
    printf("%d %d %d %d %d\n", result, s.exit_requested, s.exit_status, s.abort_list, s.last_status);
    return 0;
}
