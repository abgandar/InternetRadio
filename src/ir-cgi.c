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

// convert binary number to single hex digit
char char_to_hex( const char c )
{
    switch( c )
    {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
            return c+'0';

        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
            return c-10+'a';
    }

    return 'X';
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

// JSON encode string (result must later be free-ed by caller).
char* jsonencode( const unsigned char *str )
{
    const unsigned char *c;
    int len = 1;

    // determine resulting string length
    for( c = str; *c != '\0'; c++ )
    {
        if( *c == '\\' || *c == '"' )
            len += 2;
        else if( *c <= 0x20 )
            len += 6;
        else
            len++;
    }

    // allocate result
    unsigned char *p, *dup = (unsigned char*) malloc( len*sizeof(char) );
    if( dup == NULL ) return NULL;

    // copy or encode characters
    for( c = str, p = dup; *c != '\0'; c++ )
    {
        if( *c == '\\' || *c == '"')
        {
            *(p++) = '\\';
            *(p++) = *c;
        }
        else if( *c <= 0x20 )
        {
            *(p++) = '\\';
            *(p++) = 'u';
            *(p++) = '0';
            *(p++) = '0';
            *(p++) = char_to_hex( (*c >> 4) & 0x0F );
            *(p++) = char_to_hex( *c & 0x0F );
        }
        else
            *(p++) = *c;
    }
    *p = '\0';

    return dup;
}

// output a JSON string attribute
void json_str( const char *name, const char *value, const char comma )
{
    char *json = jsonencode( value );
    printf( "\"%s\":\"%s\"%c", name, json, comma );
    free( json );
}

// output a JSON int attribute
void json_int( const char *name, const int value, const char comma )
{
    printf( "\"%s\":\"%i\"%c", name, value, comma );
}

// start outputting results
void output_start( )
{
    static bool started = false;

    if( started ) return;   // only send once

    puts( "Content-type: application/json\n" );               // header
    puts( "{\"status\":200,\"message\":\"Request successfull\"," );  // start JSON output
    started = true;
}

// finish outputting results and end program
void output_end( )
{
    // always attach current status to output (no error if status command fails since we already sent output before!)
    struct mpd_status *status = NULL;
    struct mpd_song *song = NULL;
    char *json = NULL;
	if( mpd_command_list_begin( conn, true ) && mpd_send_status( conn ) && mpd_send_current_song( conn ) && mpd_command_list_end( conn ) && (status = mpd_recv_status( conn )) )
    {
        puts( "\"state\":{" );

        if( mpd_status_get_state( status ) == MPD_STATE_PLAY || mpd_status_get_state( status ) == MPD_STATE_PAUSE )
        {
            mpd_response_next( conn );
            song = mpd_recv_song( conn );
            puts( "\"song\":{" );
            json_int( "position", mpd_status_get_song_pos( status ), ',' );
            json_int( "id", mpd_song_get_id( song ), ',' );
            json_str( "title", mpd_song_get_tag( song, MPD_TAG_TITLE, 0 ), ',' );
            json_str( "name", mpd_song_get_tag( song, MPD_TAG_NAME, 0 ), ',' );
            json_str( "artist", mpd_song_get_tag( song, MPD_TAG_ARTIST, 0 ), ',' );
            json_str( "uri", mpd_song_get_uri( song ), ' ' );
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

        json_int( "repeat", mpd_status_get_repeat( status ) ? 1 : 0, ',' );
        json_int( "random", mpd_status_get_random( status ) ? 1 : 0, ',' );
        json_int( "single", mpd_status_get_single( status ) ? 1 : 0, ',' );
        json_int( "consume", mpd_status_get_consume( status ) ? 1 : 0, ',' );

        if( mpd_status_get_error( status ) != NULL )
            json_str( "error", mpd_status_get_error( status ), ',' );

        json_int( "volume", mpd_status_get_volume( status ), ' ' );

        puts( "}" );
		mpd_status_free( status );
        mpd_response_finish( conn );
    }
    else
    {
        puts( "state:{}" );  // need to put this to prevent trailing comma
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

// skip by the given amount
void skip( int where )
{
    bool res = false;
    switch( where )
    {
        case 1:
            res = mpd_run_next( conn );
            break;

        case -1:
            res = mpd_run_previous( conn );
            break;

        case 0:
            res = mpd_run_play( conn );
            break;
    }

    if( !res )
        error( 500, "Internal Server Error", "Error skipping songs" );

    output_start( );
}

// play given song position
void play( int position )
{
    if( position >= 0 && !mpd_run_play_pos( conn, position ) )
        error( 404, "Not found", "Invalid song position" );

    output_start( );
}

// play given song id
void playid( int id )
{
    if( id >= 0 && !mpd_run_play_id( conn, id ) )
        error( 404, "Not found", "Invalid song id" );

    output_start( );
}

// pause / unpause playback
void pausemusic( int position )
{
    struct mpd_status *status = NULL;
	if( !(status = mpd_run_status( conn )) )
        error( 500, "Internal Server Error", "Error getting status" );

    switch( mpd_status_get_state( status ) )
    {
        case MPD_STATE_PLAY:
            // pause
            mpd_run_pause( conn, 1 );
            break;

        case MPD_STATE_PAUSE:
            // unpause
            mpd_run_pause( conn, 0 );
            break;

        default:
            // start playing the given song id
            play( position );
            return;
    }

    output_start( );
}

// send list of all playlists on the server
void sendPlaylists( )
{
    struct mpd_playlist *list = NULL;

    if( !mpd_send_list_playlists( conn ) )
        error( 500, "Internal Server Error", "Error listing playlists" );

    // print all playlists
    output_start( );
    puts( "\"playlists\":[" );
    int i = 0;
    while( (list = mpd_recv_playlist( conn ) ) )
    {
        if( i )
            puts( ",{" );
        else
            puts( "{" );
        json_str( "name", mpd_playlist_get_path( list ), ' ' );
        puts( "}" );
        mpd_playlist_free( list );
        i++;
    }
    puts( "]," );

    mpd_response_finish( conn );
}

// load the specified playlist into the queue, replacing current queue
void loadPlaylist( const char *arg )
{
    mpd_run_clear( conn );

    if( !mpd_run_load( conn, arg ) )
        error( 404, "Not found", "Playlist not found" );
    play( 0 );  // start playing the first song (also starts the response output)
}

// load the music directory (recursively) into the queue, replacing current queue
void loadMusic( const char *arg )
{
    mpd_run_clear( conn );

    if( !mpd_run_load( conn, arg ) )
        error( 404, "Not found", "Directory not found" );
    play( 0 );  // start playing the first song (also starts the response output)
}

// send content of specific playlist on server
void sendPlaylist( const char *arg )
{
    struct mpd_song *song = NULL;

    if( arg )
    {
        if( !mpd_send_list_playlist_meta( conn, arg ) )
            error( 404, "Not found", "Playlist not found" );
    }
    else
    {
        if( !mpd_send_list_queue_meta( conn ) )
            error( 500, "Internal Server Error", "Error listing queue" );
    }

    // print the playlist
    output_start( );
    puts( "\"playlist\":[" );
    int i = 0;
    while( (song = mpd_recv_song( conn ) ) )
    {
        if( i )
            puts( ",{" );
        else
            puts( "{" );
        json_int( "position", i, ',' );
        json_int( "id", mpd_song_get_id( song ), ',' );
        json_str( "title", mpd_song_get_tag( song, MPD_TAG_TITLE, 0 ), ',' );
        json_str( "name", mpd_song_get_tag( song, MPD_TAG_NAME, 0 ), ',' );
        json_str( "artist", mpd_song_get_tag( song, MPD_TAG_ARTIST, 0 ), ',' );
        json_str( "uri", mpd_song_get_uri( song ), ' ' );
        puts( "}" );
        mpd_song_free( song );
        i++;
    }
    puts( "]," );

    mpd_response_finish( conn );
}

// add song(s) to playlist and send new queue
void add( char *arg )
{
	if( !mpd_command_list_begin( conn, true ) )
        error( 500, "Internal Server Error", "Error adding song" );

    char *url = strtok( arg, ":" );
    while( url )
    {
        if( !mpd_send_add( conn, url ) )
            error( 404, "Not found", "Song not found" );
        url = strtok( NULL, ":" );
    }

	if( !mpd_command_list_end( conn ) )
        error( 404, "Not found", "Song not found" );

    mpd_response_finish( conn );
    sendPlaylist( NULL );
}

// search the music database for entries matching the given search term
void searchMusic( const char *arg )
{
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
        error( 500, "Internal Server Error", "MPD connection rejected" );
#endif

    // decode command
    if( strcmp( argdec, "status" ) == 0 || strcmp( argdec, "state" ) == 0 )
    {
        // Send current state (current queue, current song, ...)
        // This is automatically added at the end to every successful request, so we do nothing except starting the output
        output_start( );
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
    else if( strcmp( argdec, "queue" ) == 0 )
    {
        // Send content of current queue
        sendPlaylist( NULL );
    }
    else if( strncmp( argdec, "load:", 5 ) == 0 )
    {
        // Load the given playlist to replace the current queue and send its content
        loadPlaylist( argdec+5 );
        sendPlaylist( NULL );
    }
    else if( strncmp( argdec, "music:", 6 ) == 0 )
    {
        // Load the given music directory (recursively) to replace the current queue and send its content
        loadMusic( argdec+6 );
        sendPlaylist( NULL );
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
    else if( strncmp( argdec, "play:", 5 ) == 0 )
    {
        // Start playback at given position
        int i = strtol( argdec+5, NULL, 10 );
        play( i );
    }
    else if( strncmp( argdec, "playid:", 7 ) == 0 )
    {
        // Start playback at given id
        int i = strtol( argdec+7, NULL, 10 );
        playid( i );
    }
    else if( strncmp( argdec, "pause:", 6 ) == 0 )
    {
        // Pause playback
        int i = strtol( argdec+6, NULL, 10 );
        pausemusic( i );
    }
    else if( strncmp( argdec, "add:", 4 ) == 0 )
    {
        // Add song to queue
        add( argdec+4 );
    }
    else if( strncmp( argdec, "search:", 7 ) == 0 )
    {
        // Search a song in the music list
        searchMusic( argdec+7 );
    }
    else
    {
        error( 400, "Bad Request", "Request not understood" );
    }

    // cleanup
    free( argdec );

    // if we reach here everything is OK
    output_end( );
    return 0;
}
