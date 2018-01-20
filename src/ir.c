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
 * ir.c
 *
 * Standalone HTTP server to connect to the mpd daemon and communicate various XML HTTP
 * requests from the client.
 *
 * Simply execute the ir binary.
 *
 */

#include <stdio.h>
#include <sys/types.h>

#include "ir-common.h"
#include "ir-server.h"

// handle CGI queries
int handle_ir_cgi( const req *c )
{
    // open output buffer
    char *obuf = NULL;
    size_t obuf_size = 0;
    if( output_start( &obuf, &obuf_size ) )
    {
        perror( "output_start" );
        exit( EXIT_FAILURE );
    }
    
    // find correct query string and run actual query
    char *query;
    if( c->m == M_POST )
        query = c->body;    // rely on body being null terminated
    else
    {
        query = c->url+15;
        query += (*query == '?');  // extract query string
    }
    debug_printf( "===> CGI query string: %s\n", query );
    int rc = 0;
    if( !rc ) rc = handleQuery( query );
    if( !rc ) rc = output_end( );   // obuf will be set even even if there was an error
    
    // split CGI output into headers (if applicable) and body
    char *head = NULL, *body = strstr( obuf, "\r\n\r\n" );
    if( body )
    {
        head = obuf;
        body[2] = '\0';     // keep one \r\n pair
        body += 4;
        obuf_size -= (body-obuf);
    }
    else
        body = obuf;
    
    write_response( c, rc ? HTTP_SERVER_ERROR : HTTP_OK, head, body, obuf_size );
    debug_printf( "===> CGI response:\n%s\n", body );
    
    // clean up
    free( obuf );
    return SUCCESS;
}

// Main HTTP server program entry point
int main( int argc, char *argv[] )
{
    const int rc = http_server_main( argc, argv );
    disconnectMPD( );
    return rc;
}
