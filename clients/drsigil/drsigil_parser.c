#include <string.h>
#include <getopt.h>
#include "drsigil.h"

static struct option long_options[] =
{
    {"num-frontend-threads", required_argument, 0, 't'},
    {"tmp-dir",              required_argument, 0, 'd'},
    {0, 0, 0, 0},
};

void
parse(int argc, char *argv[], command_line_options *clo)
{
    /* init args */
    command_line_options init;
    clo->frontend_threads = init.frontend_threads = 0;
    clo->tmp_dir = init.tmp_dir = NULL;

    int c = 0;
    int option_index = 0;

    while( (c = getopt_long(argc, argv, "t:d:", long_options, &option_index)) > 0 )
    {
        switch(c)
        {
        case 't':
            clo->frontend_threads = atoi(optarg);
            break;
        case 'd':
            clo->tmp_dir = optarg;
            break;
        default:
            break;
        }
    }

    if(clo->tmp_dir == init.tmp_dir ||
       clo->frontend_threads == init.frontend_threads)
    {
        dr_abort_w_msg("Parsing Error"); //TODO cleanup
    }
}
