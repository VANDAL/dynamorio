#include <string.h>
#include <getopt.h>
#include "drsigil.h"

static struct option long_options[] =
{
    {"standalone",           no_argument,       0, 's'},
    {"num-frontend-threads", required_argument, 0, 'n'},
    {"ipc-dir",              required_argument, 0, 'd'},
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
    *clo = (command_line_options){NULL, NULL, NULL, 0, false};

    while( (c = getopt_long(argc, argv, "sn:d:t:", long_options, &option_index)) > 0 )
    {
        switch(c)
        {
        case 'n':
            clo->frontend_threads = atoi(optarg);
            break;
        case 'd':
            clo->ipc_dir = optarg;
            break;
        case 'b':
            clo->start_func = optarg;
            break;
        case 'e':
            clo->stop_func = optarg;
            break;
        case 's':
            clo->standalone = true;
            break;
        default:
            break;
        }
    }

    if(clo->frontend_threads == 0 ||
	   (clo->standalone == false && clo->ipc_dir == NULL))
    {
        dr_abort_w_msg("Parsing Error"); //TODO cleanup
    }

    if(clo->start_func != NULL)
    {
        roi = false;
    }
}
