/*
 * Copyright (C) 2016-2018 Alexander Wittig <alexander@wittig.name>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

/**
 * ir-cgi.c
 *
 * CGI program to connect to the mpd daemon and communicate various XML HTTP
 * requests from the client.
 *
 * Simply make the ir.cgi binary executable by your web server.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "ir-common.h"

// HTTP error numbers and messages (defined in ir-common.c)
extern const char* const SERVER_ERROR_MSG;
extern const int SERVER_ERROR;
extern const char* const BAD_REQUEST_MSG;
extern const int BAD_REQUEST;
extern const char* const FORBIDDEN_MSG;
extern const int FORBIDDEN;
extern const char* const NOT_FOUND_MSG;
extern const int NOT_FOUND;

// Main CGI program entry point
int main( int argc, char *argv[] )
{
    // open output buffer
    char *obuf = NULL;
    size_t obuf_size = 0;

    if( output_start( &obuf, &obuf_size ) )         // in case of failure, outbuf is automatically set to stdout
        return error( SERVER_ERROR, SERVER_ERROR_MSG, "Request failed" );

    // get query string from CGI environment and duplicate so it is writeable
    int rc = 0;
    char *arg = NULL;
    char *env = getenv( "QUERY_STRING" );

    if( env == NULL )
    {
        // attempt to use command line argument(s) to simplify use as stand alone tool (similar to mpc)
        if( argc < 2 )
            rc = error( BAD_REQUEST, BAD_REQUEST_MSG, "Request incomplete" );
        else
            env = argv[1];
    }

    if( env )
    {
        arg = strdup( env );
        if( arg == NULL )
            rc = error( SERVER_ERROR, SERVER_ERROR_MSG, "Request failed" );
    }

    // handle query
    if( !rc ) rc = handleQuery( arg );

    // write output
    if( !rc ) rc = output_end( );
    fwrite( obuf, obuf_size, 1, stdout );   // either error or output_end will have closed output buffer stream

    // clean up
    free( obuf );
    free( arg );
    disconnectMPD( );

    return rc;
}
