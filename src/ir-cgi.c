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
#include <stdbool.h>

#include <mpd/client.h>

#include "config.h"

static struct mpd_connection *conn = NULL;

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
    puts( "{\"status\":200,\"message\":\"Request successfull\"," );  // start JSON output
}

// finish outputting results and end program
void output_end( )
{
    // always attach current status to output (no error if status command fails since we already sent output before!)
    struct mpd_status *status = NULL;
    struct mpd_song *song = NULL;
	if( mpd_command_list_begin( conn, true ) && mpd_send_status( conn ) && mpd_send_current_song( conn ) && mpd_command_list_end( conn ) && (status = mpd_recv_status( conn )) )
    {
        puts( "\"stat\":{" );

        if( mpd_status_get_state( status ) == MPD_STATE_PLAY || mpd_status_get_state( status ) == MPD_STATE_PAUSE )
        {
            mpd_response_next( conn );
            song = mpd_recv_song( conn );
            puts( "\"song\":{" );
            printf( "\"position\":%i,\n", mpd_status_get_song_pos( status ) );
            printf( "\"id\":%i,\n", mpd_song_get_id( song ) );
            printf( "\"title\":\"%s\",\n", mpd_song_get_tag( song, MPD_TAG_TITLE, 0 ) );
            printf( "\"name\":\"%s\",\n", mpd_song_get_tag( song, MPD_TAG_NAME, 0 ) );
            printf( "\"artist\":\"%s\",\n", mpd_song_get_tag( song, MPD_TAG_ARTIST, 0 ) );
            printf( "\"uri\":\"%s\"\n", mpd_song_get_uri( song ) );
            puts( "}," );
            mpd_song_free( song );
        }

		switch( mpd_status_get_state( status ) )
        {
            case MPD_STATE_PLAY:
                puts( "\"playing\":1,");
                break;
            case MPD_STATE_PAUSE:
                puts( "\"playing\":0,");
                break;
            default:
                puts( "\"playing\":0,");
        }

        printf( "\"repeat\":%i,", mpd_status_get_repeat( status ) ? 1 : 0 );
        printf( "\"random\":%i,", mpd_status_get_random( status ) ? 1 : 0 );
        printf( "\"single\":%i,", mpd_status_get_single( status ) ? 1 : 0 );
        printf( "\"consume\":%i,", mpd_status_get_consume( status ) ? 1 : 0 );

        if( mpd_status_get_error( status ) != NULL )
            printf( "\"error\":\"%s\",", mpd_status_get_error( status ) );

        printf( "\"volume\":%i", mpd_status_get_volume( status ));  // no trailing comma for last entry

        puts( "}" );
		mpd_status_free( status );
        mpd_response_finish( conn );
    }
    else
    {
        puts( "stat:{}" );  // need to put this to prevent trailing comma
    }

    puts( "}" );  // end JSON output
    if( conn ) mpd_connection_free( conn );
    exit( 0 );
}

// output an error and exit
void error( const int code, const char* msg, const char* message )
{
    printf( "Status: %d %s\nContent-type: application/json\n\n", code, msg );               // header
    printf( "{\"status\":%d,\"message\":\"%s\"}", code, message != NULL ? message : msg );  // JSON
    if( conn ) mpd_connection_free( conn );
    exit( code );
}

#define NOT_YET_IMPLEMENTED puts( "Not yet implemented" )

// search the music database for entries matching the given search term
void searchMusic( const char *arg )
{
    NOT_YET_IMPLEMENTED;
}

// send list of all playlists on the server
void sendPlaylists( )
{
    NOT_YET_IMPLEMENTED;
}

// send content of specific playlist on server
void sendPlaylist( const char *arg )
{
    struct mpd_song *song = NULL;

    if( !mpd_send_list_playlist( conn, arg ) )
        error( 404, "Not found", "Playlist not found" );

    // print the playlist
    puts( "playlist:[" );
    int i = 0;
    while( (song = mpd_recv_song( conn ) ) )
    {
        if( i )
            puts( ",{" );
        else
            puts( "{" );
        printf( "\"position\":%i,\n", i );
        printf( "\"id\":%i,\n", mpd_song_get_id( song ) );
        printf( "\"title\":\"%s\",\n", mpd_song_get_tag( song, MPD_TAG_TITLE, 0 ) );
        printf( "\"name\":\"%s\",\n", mpd_song_get_tag( song, MPD_TAG_NAME, 0 ) );
        printf( "\"artist\":\"%s\",\n", mpd_song_get_tag( song, MPD_TAG_ARTIST, 0 ) );
        printf( "\"uri\":\"%s\"\n", mpd_song_get_uri( song ) );
        puts( "}" );
        mpd_song_free( song );
        i++;
    }
    puts( "]," );

    mpd_response_finish( conn );
}

// load the specified playlist into the queue, replacing current queue
void loadPlaylist( const char *arg )
{
    NOT_YET_IMPLEMENTED;
}

// skip by the given amount
void skip( int where )
{
    NOT_YET_IMPLEMENTED;
}

// play/pause playback
void play( int state )
{
    NOT_YET_IMPLEMENTED;
}

// play/pause playback
void add( const char *arg )
{
    NOT_YET_IMPLEMENTED;
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

    // Open connection to MPD
    conn = mpd_connection_new( SOCKET_PFAD, 0, 0 );
    if( conn == NULL || mpd_connection_get_error( conn ) != MPD_ERROR_SUCCESS )
        error( 500, "Internal Server Error", "No connection to MPD" );

#ifdef MPD_PASSWORT
    // send password if there is one (unencrypted clear text, mostly window dressing)
    if( !mpd_run_password( conn, MPD_PASSWORT ) )
    {
        mpd_connection_free( conn );
        error( 500, "Internal Server Error", "MPD connection rejected" );
    }
#endif

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
    else if( strncmp( argdec, "load:", 5 ) == 0 )
    {
        // Load the given playlist to replace the current queue and send its content
        loadPlaylist( argdec+5 );
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
        play( true );
    }
    else if( strcmp( argdec, "pause" ) == 0 )
    {
        // Pause playback
        play( false );
    }
    else if( strncmp( argdec, "add:", 4 ) == 0 )
    {
        // Send current state (current queue, current song, ...)
        add( argdec+4 );
    }
    else if( strcmp( argdec, "status" ) == 0 )
    {
        // Send current state (current queue, current song, ...)
        // This is automatically added at the end to every successful request, so we do nothing
    }
    else
    {
        error( 400, "Bad Request", "Requested action not understood" );
    }

    // cleanup
    free( argdec );

    // if we reach here everything is OK
    output_end( );
    return 0;
}
