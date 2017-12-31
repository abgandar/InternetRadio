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

#define __USE_POSIX

#include "config.h"

#include <stdio.h>
#include <stdio_ext.h>  // __fpurge is some silly linux extension
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/reboot.h>
#ifdef SYSTEMD
#include <systemd/sd-bus.h>
#endif
#include <mpd/client.h>

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
char* jsonencode( const char *str )
{
    const char *c;
    int len = 1;

    // NULL is interpreted as the empty string
    if( str == NULL )
    {
        char *p = (char*)malloc( 1*sizeof(char) );
        *p = '\0';
        return p;
    }

    // determine resulting string length
    for( c = str; *c != '\0'; c++ )
    {
        if( *c == '\\' || *c == '"' )
            len += 2;
        else if( (unsigned char)*c < 0x20 )
            len += 6;
        else
            len++;
    }

    // allocate result
    char *p, *dup = (char*) malloc( len*sizeof(char) );
    if( dup == NULL ) return NULL;

    // copy or encode characters
    for( c = str, p = dup; *c != '\0'; c++ )
    {
        if( *c == '\\' || *c == '"')
        {
            *(p++) = '\\';
            *(p++) = *c;
        }
        else if( (unsigned char)*c < 0x20 )
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
    if( comma )
        printf( "\"%s\":\"%s\"%c", name, json, comma );
    else
        printf( "\"%s\":\"%s\"", name, json );
    free( json );
}

// output a JSON int attribute
void json_int( const char *name, const int value, const char comma )
{
    if( comma )
        printf( "\"%s\":%i%c", name, value, comma );
    else
        printf( "\"%s\":%i", name, value );
}

// start outputting results
void output_start( )
{
    static bool started = false;

    if( started ) return;   // only send once

    fputs( "Content-type: application/json\nCache-control: no-cache\n\n", stdout );                      // header
    fputs( "{\"status\":200,\"message\":\"Request successful\",", stdout );    // start JSON output

    started = true;
}

// finish outputting results and end program
void output_end( )
{
    // always attach our hostname (just for good measure)
    char host[HOST_NAME_MAX+1];
    if( gethostname( host, sizeof( host ) ) == 0 )
        json_str( "host", host, ',' );

    // always attach current status to output (no error if status command fails since we may already have sent other output before)
    struct mpd_status *status = NULL;
    struct mpd_song *song = NULL;
	if( mpd_command_list_begin( conn, true ) && mpd_send_status( conn ) && mpd_send_current_song( conn ) && mpd_command_list_end( conn ) && (status = mpd_recv_status( conn )) )
    {
        fputs( "\"state\":{", stdout );

        if( mpd_status_get_state( status ) == MPD_STATE_PLAY || mpd_status_get_state( status ) == MPD_STATE_PAUSE )
        {
            mpd_response_next( conn );
            song = mpd_recv_song( conn );
            fputs( "\"song\":{", stdout );
            json_int( "pos", mpd_status_get_song_pos( status ), ',' );
            json_int( "id", mpd_song_get_id( song ), ',' );
            json_str( "title", mpd_song_get_tag( song, MPD_TAG_TITLE, 0 ), ',' );
            json_str( "name", mpd_song_get_tag( song, MPD_TAG_NAME, 0 ), ',' );
            json_str( "artist", mpd_song_get_tag( song, MPD_TAG_ARTIST, 0 ), ',' );
            json_str( "uri", mpd_song_get_uri( song ), ' ' );
            fputs( "},", stdout );
            mpd_song_free( song );
        }

		switch( mpd_status_get_state( status ) )
        {
            case MPD_STATE_PLAY:
                fputs( "\"playing\":1,", stdout );
                break;
            case MPD_STATE_PAUSE:
                fputs( "\"playing\":0,", stdout );
                break;
            default:
                fputs( "\"playing\":0,", stdout );
        }

        json_int( "repeat", mpd_status_get_repeat( status ) ? 1 : 0, ',' );
        json_int( "random", mpd_status_get_random( status ) ? 1 : 0, ',' );
        json_int( "single", mpd_status_get_single( status ) ? 1 : 0, ',' );
        json_int( "consume", mpd_status_get_consume( status ) ? 1 : 0, ',' );

        if( mpd_status_get_error( status ) != NULL )
            json_str( "error", mpd_status_get_error( status ), ',' );

        json_int( "volume", mpd_status_get_volume( status ), ' ' );

        fputs( "}", stdout );
		mpd_status_free( status );
        mpd_response_finish( conn );
    }
    else
    {
        fputs( "state:{}", stdout );  // need to put this to prevent trailing comma
    }

    fputs( "}", stdout );  // end JSON output
    fflush( stdin );
    if( conn ) mpd_connection_free( conn );
    exit( 0 );
}

// output an error and exit
void error( const int code, const char* msg, const char* message )
{
    char *m;
    if( message == NULL )
    {
        if( mpd_connection_get_error( conn ) != MPD_ERROR_SUCCESS )
            m = jsonencode( mpd_connection_get_error_message( conn ) );
        else
            m = strdup( "-" );
    }
    else
        m = jsonencode( message );

    __fpurge( stdout );     // discard any previous buffered output
    printf( "Status: %d %s\nContent-type: application/json\nCache-control: no-cache\n\n", code, msg );
    printf( "{\"status\":%d,\"message\":\"%s\"}", code, m );
    fflush( stdout );
    free( m );
    if( conn ) mpd_connection_free( conn );
    exit( code );
}

// set mixer volume for all outputs to value between 0 and 100
void setVolume( const unsigned int vol )
{
    if( !mpd_run_set_volume( conn, vol ) )
        error( 500, "Internal Server Error", NULL );
}

// skip by the given amount
void skip( const int where )
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
}

// play given song position
void play( const int position )
{
    if( position >= 0 && !mpd_run_play_pos( conn, position ) )
        error( 404, "Not found", NULL );
}

// play given song id
void playid( const int id )
{
    if( id >= 0 && !mpd_run_play_id( conn, id ) )
        error( 404, "Not found", NULL );
}

// pause / unpause playback
void pausemusic( const int position )
{
    struct mpd_status *status = NULL;
	if( !(status = mpd_run_status( conn )) )
        error( 500, "Internal Server Error", NULL );

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
            break;
    }
}

// send list of all playlists on the server
void sendPlaylists( )
{
    struct mpd_playlist *list = NULL;
    
    if( !mpd_send_list_playlists( conn ) )
    error( 500, "Internal Server Error", NULL );
    
    // print all playlists
    output_start( );
    fputs( "\"playlists\":[", stdout );
    int i = 0;
    while( (list = mpd_recv_playlist( conn ) ) )
    {
        if( i )
        fputs( ",{", stdout );
        else
        fputs( "{", stdout );
        json_str( "name", mpd_playlist_get_path( list ), ' ' );
        fputs( "}", stdout );
        mpd_playlist_free( list );
        i++;
    }
    fputs( "],", stdout );
    
    mpd_response_finish( conn );
}

// send content of specific playlist on server
void sendPlaylist( const char *arg )
{
    struct mpd_song *song = NULL;
    
    if( arg )
    {
        if( !mpd_send_list_playlist_meta( conn, arg ) )
        error( 404, "Not found", NULL );
    }
    else
    {
        if( !mpd_send_list_queue_meta( conn ) )
        error( 500, "Internal Server Error", NULL );
    }
    
    // print the playlist
    output_start( );
    fputs( "\"playlist\":[", stdout );
    int i = 0;
    while( (song = mpd_recv_song( conn ) ) )
    {
        if( i )
        fputs( ",{", stdout );
        else
        fputs( "{", stdout );
        json_int( "position", i, ',' );
        json_int( "id", mpd_song_get_id( song ), ',' );
        json_str( "title", mpd_song_get_tag( song, MPD_TAG_TITLE, 0 ), ',' );
        json_str( "name", mpd_song_get_tag( song, MPD_TAG_NAME, 0 ), ',' );
        json_str( "artist", mpd_song_get_tag( song, MPD_TAG_ARTIST, 0 ), ',' );
        json_str( "track", mpd_song_get_tag( song, MPD_TAG_TRACK, 0 ), ',' );
        json_str( "album", mpd_song_get_tag( song, MPD_TAG_ALBUM, 0 ), ',' );
        json_str( "uri", mpd_song_get_uri( song ), ' ' );
        fputs( "}", stdout );
        mpd_song_free( song );
        i++;
    }
    fputs( "],", stdout );
    
    mpd_response_finish( conn );
}

// load the specified playlist into the queue, replacing current queue
void loadPlaylist( const char *arg )
{
    mpd_run_clear( conn );
    if( !mpd_run_load( conn, arg ) )
        error( 404, "Not found", NULL );
#ifdef AUTOPLAY
    mpd_run_play_pos( conn, 0 );  // try to autoplay the first song
#endif
    sendPlaylist( NULL );
}

// load part of the music directory (recursively) into the queue, replacing current queue
void loadMusic( const char *arg )
{
    mpd_run_clear( conn );
    mpd_search_add_db_songs( conn, false );
    mpd_search_add_uri_constraint( conn, MPD_OPERATOR_DEFAULT, arg ); // matches against full file name relative to music dir or uri. empty arg allowed.
//    if( arg && *arg != '\0' )
//        mpd_search_add_base_constraint( conn, MPD_OPERATOR_DEFAULT, arg );  // restrict search to subdirectory of music dir, must be non-empty, error if directory does not exist
    mpd_search_add_sort_tag( conn, MPD_TAG_ARTIST_SORT, false );    // are multiple sort tags supported?
    //mpd_search_add_sort_tag( conn, MPD_TAG_ARTIST, false );
    mpd_search_add_sort_tag( conn, MPD_TAG_TITLE, false );
    if( !mpd_search_commit( conn ) )
        error( 404, "Not found", NULL );
    if( !mpd_response_finish( conn ) )
        error( 404, "Not found", NULL );
#ifdef AUTOPLAY
    mpd_run_play_pos( conn, 0 );  // try to autoplay the first song
#endif
    sendPlaylist( NULL );
}

// add song(s) to playlist and send new queue
void add( char *arg )
{
    if( !mpd_command_list_begin( conn, false ) )
        error( 500, "Internal Server Error", NULL );

    char *url;
    while( (url = strsep( &arg, "|" )) )
    {
        if( !mpd_send_add( conn, url ) )
            error( 404, "Not found", NULL );
    }

    if( !mpd_command_list_end( conn ) )
        error( 404, "Not found", NULL );

    mpd_response_finish( conn );
    sendPlaylist( NULL );
}

// send password (unencrypted clear text, mostly window dressing)
void sendPassword( const char *arg )
{
    if( !mpd_run_password( conn, arg ) )
        error( 403, "Forbidden", NULL );
}

// reboot system (if priviliges allow)
void rebootSystem( const int mode )
{
#ifdef SYSTEMD
    sd_bus *bus = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r;

    /* Connect to the system bus (adapted from http://0pointer.net/blog/the-new-sd-bus-api-of-systemd.html) */
    r = sd_bus_open_system( &bus );
    if( r < 0 )
        error( 500, "Internal Server Error", strerror( -r ) );
    r = sd_bus_call_method( bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                            "org.freedesktop.systemd1.Manager",
                            mode == RB_POWER_OFF ? "PowerOff" : "Reboot",
                            &err, NULL, NULL );
    if( r < 0 )
    {
        sd_bus_unref( bus );
        error( 500, "Internal Server Error", err.message );
        sd_bus_error_free( &err );    // not reached
    }
#else
    sync( );
    usleep( REBOOT_WAIT );     // wait for buffers to flush. Not done in systemctl reboot.
    reboot( mode );
#endif
    error( 500, "Internal Server Error", "Shutdown or reboot failed" );     // not reached
}

// Parse a command
void parseCommand( char *cmd )
{
    if( strncmp( cmd, "password=", 9 ) == 0 )
    {
        // Send password
        sendPassword( cmd+9 );
    }
    else if( strcmp( cmd, "state" ) == 0 )
    {
        // Send current state (current queue, current song, ...)
        // This is automatically added at the end to every successful request, so we do nothing
        return;
    }
    else if( strcmp( cmd, "playlists" ) == 0 )
    {
        // Send list of all playlists on server
        sendPlaylists( );
    }
    else if( strncmp( cmd, "playlist=", 9 ) == 0 )
    {
        // Send content of given playlist
        sendPlaylist( cmd+9 );
    }
    else if( strcmp( cmd, "queue" ) == 0 )
    {
        // Send content of current queue
        sendPlaylist( NULL );
    }
    else if( strncmp( cmd, "load=", 5 ) == 0 )
    {
        // Load the given playlist to replace the current queue and send its content
        loadPlaylist( cmd+5 );
    }
    else if( strncmp( cmd, "music=", 6 ) == 0 )
    {
        // Load the given music directory (recursively) to replace the current queue and send its content
        loadMusic( cmd+6 );
    }
    else if( strcmp( cmd, "forward" ) == 0 )
    {
        // Jump to next item on playlist
        skip( 1 );
    }
    else if( strcmp( cmd, "back" ) == 0 )
    {
        // Jump to previous item on playlist
        skip( -1 );
    }
    else if( strncmp( cmd, "play=", 5 ) == 0 )
    {
        // Start playback at given position
        const int i = strtol( cmd+5, NULL, 10 );
        play( i );
    }
    else if( strncmp( cmd, "playid=", 7 ) == 0 )
    {
        // Start playback at given id
        const int i = strtol( cmd+7, NULL, 10 );
        playid( i );
    }
    else if( strncmp( cmd, "pause=", 6 ) == 0 )
    {
        // Pause playback
        const int i = strtol( cmd+6, NULL, 10 );
        pausemusic( i );
    }
    else if( strncmp( cmd, "add=", 4 ) == 0 )
    {
        // Add song to queue
        add( cmd+4 );
    }
    else if( strncmp( cmd, "volume=", 7 ) == 0 )
    {
        // Set the mixer volume
        const unsigned int i = strtol( cmd+7, NULL, 10 );
        setVolume( i );
    }
    else if( strcmp( cmd, "reboot" ) == 0 )
    {
        // Reboot the system (assuming sufficient priviliges)
        rebootSystem( RB_AUTOBOOT );
    }
    else if( strcmp( cmd, "shutdown" ) == 0 )
    {
        // Reboot the system (assuming sufficient priviliges)
        rebootSystem( RB_POWER_OFF );
    }
    else
    {
        // No idea what you want from me
        error( 400, "Bad Request", "Request not understood" );
    }
}

// Main program entry point
int main( int argc, char *argv[] )
{
    // set up a large output buffer to allow errors occuring later to purge previous output
    setvbuf( stdout, NULL, _IOFBF, OUTPUT_BUFFER_SIZE );

    // get query string from CGI environment
    const char *env = getenv( "QUERY_STRING" );
    if( env == NULL )
        error( 400, "Bad Request", "Request incomplete" );

    // Open connection to MPD
    conn = mpd_connection_new( SOCKET_PFAD, 0, 0 );
    if( conn == NULL || mpd_connection_get_error( conn ) != MPD_ERROR_SUCCESS )
        error( 500, "Internal Server Error", NULL );

    // duplicate query string so it is writeable
    char *arg = strdup( env );
    if( arg == NULL )
        error( 500, "Internal Server Error", "Request failed" );

    // handle each request separately
    char *var, *str = arg;
    while( (var = strsep( &str, "&" )) != NULL )
    {
        // URL decode argument
        char *argdec = urldecode( var );
        if( argdec == NULL )
            error( 500, "Internal Server Error", "Request failed" );
        parseCommand( argdec );
        free( argdec );
    }
    free( arg );

    // if we reach here everything is OK.
    output_start( );
    output_end( );

    return 0;
}
