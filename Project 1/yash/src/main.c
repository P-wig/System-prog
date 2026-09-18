#include <stdio.h>
#include <string.h>

#include "builtins.h"
#include "exec.h"
#include "jobs.h"
#include "parse.h"
#include "sig.h"
#include "yash.h"

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
    return 0;
}
