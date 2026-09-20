#include <stdio.h>
#include <string.h>

#include "builtins.h"
#include "exec.h"
#include "jobs.h"
#include "parse.h"
#include "sig.h"
#include "yash.h"

/*
 * C library interfaces used here:
 *   fgets(buf, n, stdin) - reads at most n-1 bytes up to and including '\n'.
 *                          Returns NULL on end-of-file AND on error; the two
 *                          are told apart with feof()/ferror().
 *   feof(stdin)          - true only if the stream really hit EOF (Ctrl-d).
 *   clearerr(stdin)      - clears the error/EOF flags so the stream stays
 *                          usable after a signal interrupted the read (EINTR).
 *   fputs(s, stdout)     - writes a string with no trailing newline added.
 *   fflush(stdout)       - forces buffered output out now; needed because the
 *                          prompt has no '\n' and would otherwise sit in the
 *                          buffer until later.
 *   putchar(c)           - writes one character to stdout.
 *   strcspn(s, "\n")     - length of the prefix of s containing no '\n', i.e.
 *                          the index of the newline, used to trim it.
 */

/*
 * The read-eval loop. Misc CRITERIA land here:
 *  - "The prompt must be printed as a '# '" -> fputs("# ") with no newline,
 *    followed by fflush so it appears before the blocking read.
 *  - "All child processes will be dead on exit" -> jobs_kill_all() runs on the
 *    way out, covering both background and Ctrl-z'd jobs.
 *  - "Ctrl-d will exit the shell" -> fgets returns NULL with feof set.
 * The other two Misc items are properties of execvp() in exec.c: the child
 * inherits the parent's environment across fork/exec, and the `p` in execvp
 * searches PATH for every executable.
 */
int main(void)
{
    char  line[YASH_MAX_LINE + 2];
    job_t job;

    sig_init_shell();
    jobs_init();

    for (;;) {
        jobs_reap();

        fputs("# ", stdout);
        fflush(stdout);

        /* Ctrl-d (EOF) ends the shell. A signal-interrupted read just reprompts. */
        if (fgets(line, sizeof line, stdin) == NULL) {
            if (feof(stdin))
                break;
            clearerr(stdin);
            putchar('\n');
            continue;
        }

        line[strcspn(line, "\n")] = '\0';

        if (parse_line(line, &job) != PARSE_OK)
            continue;

        if (builtin_try(&job))
            continue;

        exec_job(&job);
    }

    putchar('\n');
    jobs_kill_all();
    return 0;
}
