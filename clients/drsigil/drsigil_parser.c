#include <string.h>
#include <getopt.h>
#include "drsigil.h"

static struct option long_options[] =
{
    {"num-frontend-threads", required_argument, 0, 'n'},
    {"tmp-dir",              required_argument, 0, 'd'},
    {"uid",                  required_argument, 0, 'u'},
    {"start-func",           required_argument, 0, 'b'},
    {"stop-func",            required_argument, 0, 'e'},
    {0, 0, 0, 0},
};

void
parse(int argc, char *argv[], command_line_options *clo)
{
    /* init args */

    int c = 0;
    int option_index = 0;
    *clo = (command_line_options){0, NULL, NULL, NULL, NULL};

    while( (c = getopt_long(argc, argv, "n:d:t:", long_options, &option_index)) > 0 )
    {
        switch(c)
        {
        case 'n':
            clo->frontend_threads = atoi(optarg);
            break;
        case 'd':
            clo->tmp_dir = optarg;
            break;
        case 'u':
            clo->uid = optarg;
            break;
        case 'b':
            clo->start_func = optarg;
            break;
        case 'e':
            clo->stop_func = optarg;
            break;
        default:
            break;
        }
    }

    if(clo->tmp_dir == NULL || clo->frontend_threads == 0 || clo->uid == NULL)
    {
        dr_abort_w_msg("Parsing Error"); //TODO cleanup
    }

    if(clo->start_func != NULL)
    {
        roi = false;
    }
}
