/*
 * Copyright (C) 2016-2017 Alexander Wittig <alexander@wittig.name>
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
#include <fcntl.h>
#include <unistd.h>

#include "config.h"

// convert single hex digit to binary number
char hex_to_char( const char c )
{
    switch( c )
    {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return c-'0';

        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
            return c-'A'+10;

        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
            return c-'a'+10;
    }

    return -1;
}

// URL decode entire string (result must later be free-ed by caller).
// Invalid %XX escape sequences are ignored and produce no output.
char* urldecode( const char *str )
{
    char *dup = strdup( str ), *p = dup, *q = dup;

    // literal copy up to next % or end of string
    for( ; *p != '\0' && *p != '%'; p++, q++ ) *q = *p;
    while( *p != '\0' )
    {
        // we're at a % sign, URL decode this sequence
        p++;
        const char c1 = hex_to_char( *p );
        if( *p != '\0' ) p++;
        const char c2 = hex_to_char( *p );
        if( *p != '\0' ) p++;
        if( c1 != -1 && c2 != -1 )
        {
            *q = c1<<4 | c2;
            q++;
        }

        // literal copy up to next % or end of string
        for( ; *p != '\0' && *p != '%'; p++, q++ ) *q = *p;
    }
    *q = '\0';

    return dup;
}

// start outputting results
void output_start( )
{
    puts( "Content-type: application/json\n" );               // header
    puts( "{\"status\":200,\"message\":\"Request successfull\",\"data\":{" );  // start JSON output
}

// finish outputting results and end program
void output_end( )
{
    puts( "}}" );  // end JSON output
    exit( 0 );
}

// output result
void output( const char* data )
{
    output_start( );
    if( data ) puts( data );
    output_end( );
}

// output an error and exit
void error( const int code, const char* msg, const char* message )
{
    fprint( "Status: %d %s\nContent-type: application/json\n\n", code, msg );               // header
    fprint( "{\"status\":%d,\"message\":\"%s\"}", code, message != NULL ? message : msg );  // JSON
    exit( code );
}

// Main program entry point
int main( int argc, char *argv[] )
{
    // get query string from CGI environment
    const char *arg = getenv( "QUERY_STRING" );
    if( arg == NULL )
        error( 400, "Bad Request", "Request incomplete" );

    // URL decode argument
    char *argdec = urldecode( arg );
    if( argdec == NULL )
        error( 500, "Internal Server Error", "Request failed" );

    // decode command
    if( strncmp( argdec, "search:", 7 ) == 0 )
    {
        // Search a song in the music list
        searchMusic( argdec+7 );
    }
    else if( strcmp( argdec, "playlists" ) == 0 )
    {
        // Send list of all playlists on server
        sendPlaylists( );
    }
    else if( strncmp( argdec, "playlist:", 9 ) == 0 )
    {
        // Send content of given playlist
        sendPlaylist( argdec+9 );
    }
    else if( strcmp( argdec, "forward" ) == 0 )
    {
        // Jump to next item on playlist
        skip( 1 );
    }
    else if( strcmp( argdec, "back" ) == 0 )
    {
        // Jump to previous item on playlist
        skip( -1 );
    }
    else if( strncmp( argdec, "skip:", 5 ) == 0 )
    {
        // Jump a given number of songs in current playlist
        int i = strtol( argdec+5, NULL, 10 );
        skip( i );
    }
    else if( strcmp( argdec, "play" ) == 0 )
    {
        // Start playback
        play( );
    }
    else if( strcmp( argdec, "pause" ) == 0 )
    {
        // Pause playback
        pause( );
    }
    else if( strcmp( argdec, "status" ) == 0 )
    {
        // Send current state (current queue, current song, ...)
        sendStatus( );
    }

    // cleanup
    free( argdec );

    // success, no data to send
    output( NULL );
    return 0;
}
