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
 * CGI program to connect to the mpd daemon and communicate various XML HTTP
 * requests from the client.
 *
 * Simply make the ir.cgi binary executable by your web server.
 *
 */

#define __USE_POSIX

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/reboot.h>
#ifdef SYSTEMD
#include <systemd/sd-bus.h>
#endif
#include <mpd/client.h>

// global variables for output and MPD connection
static struct mpd_connection *conn = NULL;
static FILE *outbuf = NULL;

// (HTTP) error numbers and messages
static const int SUCCESS = 0;
static const int WAIT_FOR_DATA = 1;
static const char* const SERVER_ERROR_MSG = "Internal server error";
static const int SERVER_ERROR = 500;
static const char* const BAD_REQUEST_MSG = "Bad request";
static const int BAD_REQUEST = 400;
static const char* const FORBIDDEN_MSG = "Forbidden";
static const int FORBIDDEN = 403;
static const char* const NOT_FOUND_MSG = "Not found";
static const int NOT_FOUND = 404;

// ========= Low level I/O

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

        // copy up to next % or end of string, replacing + by space (for POST request-like decoding)
        for( ; *p != '\0' && *p != '%'; p++, q++ )
        {
            *q = *p;
            if( *q == '+' ) *q = ' ';
        }
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
        fprintf( outbuf, "\"%s\":\"%s\"%c", name, json, comma );
    else
        fprintf( outbuf, "\"%s\":\"%s\"", name, json );
    free( json );
}

// output a JSON int attribute
void json_int( const char *name, const int value, const char comma )
{
    if( comma )
        fprintf( outbuf, "\"%s\":%i%c", name, value, comma );
    else
        fprintf( outbuf, "\"%s\":%i", name, value );
}

// ========= Low level MPD helpers

// Ensure a clean connection to MPD is open. Does not output error message on failure!
int connectMPD( )
{
    // is there a previous connection?
    if( conn )
    {
        // clear errors if possible, else disconnect
        if( mpd_connection_clear_error( conn ) )
            return SUCCESS;
        mpd_connection_free( conn );
        conn = NULL;
    }
    
    // Open new connection to MPD
    conn = mpd_connection_new( SOCKET_PFAD, 0, 0 );
    if( (conn == NULL) || (mpd_connection_get_error( conn ) != MPD_ERROR_SUCCESS) )
        return SERVER_ERROR;
    mpd_connection_set_keepalive( conn, true );

    return SUCCESS;
}

// Close connection to MPD
void disconnectMPD( )
{
    if( conn ) mpd_connection_free( conn );
    conn = NULL;
}

// ========= I/O routines

// reset buffered output and output an error instead, then close the output buffer
int error( const int code, const char* msg, const char* message )
{
    char *m;
    
    if( message == NULL )
    {
        if( conn && (mpd_connection_get_error( conn ) != MPD_ERROR_SUCCESS) )
            m = jsonencode( mpd_connection_get_error_message( conn ) );
        else
            m = strdup( "???" );
    }
    else
        m = jsonencode( message );
    
    rewind( outbuf );
    fprintf( outbuf, "Status: %d %s\r\nContent-type: application/json\r\nCache-control: no-cache\r\n\r\n", code, msg );
    fprintf( outbuf, "{\"status\":%d,\"message\":\"%s\"}\n", code, m );
    fclose( outbuf );
    free( m );
    
    return code;
}

// initialize output buffer and print HTTP/CGI headers
int output_start( char **obuf, size_t *obuf_size )
{
    if( !(outbuf = open_memstream( obuf, obuf_size )) )
       return SERVER_ERROR;

    // allocate one memory page right away
    fseek( outbuf, SEEK_SET, 4095 );    // allow for the implicit extra NULL
    rewind( outbuf );

    fputs( "Content-type: application/json\r\nCache-control: no-cache\r\n\r\n", outbuf );     // header
    fputs( "{\"status\":200,\"message\":\"Request successful\",", outbuf );             // start JSON output

    return SUCCESS;
}

// finish outputting results, general stats, and close the output buffer
int output_end( )
{
    // always attach our hostname (just for good measure)
    char host[HOST_NAME_MAX+1];
    if( gethostname( host, sizeof( host ) ) == 0 )
        json_str( "host", host, ',' );

    // always attach current status to output (no error if status command fails since we may already have sent other output before)
    struct mpd_status *status = NULL;
    struct mpd_song *song = NULL;
	if( !connectMPD( ) && mpd_command_list_begin( conn, true ) && mpd_send_status( conn ) && mpd_send_current_song( conn ) && mpd_command_list_end( conn ) && (status = mpd_recv_status( conn )) )
    {
        fputs( "\"state\":{", outbuf );

        if( mpd_status_get_state( status ) == MPD_STATE_PLAY || mpd_status_get_state( status ) == MPD_STATE_PAUSE )
        {
            mpd_response_next( conn );
            song = mpd_recv_song( conn );
            fputs( "\"song\":{", outbuf );
            json_int( "pos", mpd_status_get_song_pos( status ), ',' );
            json_int( "id", mpd_song_get_id( song ), ',' );
            json_str( "title", mpd_song_get_tag( song, MPD_TAG_TITLE, 0 ), ',' );
            json_str( "name", mpd_song_get_tag( song, MPD_TAG_NAME, 0 ), ',' );
            json_str( "artist", mpd_song_get_tag( song, MPD_TAG_ARTIST, 0 ), ',' );
            json_str( "track", mpd_song_get_tag( song, MPD_TAG_TRACK, 0 ), ',' );
            json_str( "album", mpd_song_get_tag( song, MPD_TAG_ALBUM, 0 ), ',' );
            json_str( "uri", mpd_song_get_uri( song ), ' ' );
            fputs( "},", outbuf );
            mpd_song_free( song );
        }

		switch( mpd_status_get_state( status ) )
        {
            case MPD_STATE_PLAY:
                fputs( "\"playing\":1,", outbuf );
                break;
            case MPD_STATE_PAUSE:
                fputs( "\"playing\":0,", outbuf );
                break;
            default:
                fputs( "\"playing\":0,", outbuf );
        }

        json_int( "repeat", mpd_status_get_repeat( status ) ? 1 : 0, ',' );
        json_int( "random", mpd_status_get_random( status ) ? 1 : 0, ',' );
        json_int( "single", mpd_status_get_single( status ) ? 1 : 0, ',' );
        json_int( "consume", mpd_status_get_consume( status ) ? 1 : 0, ',' );

        if( mpd_status_get_error( status ) != NULL )
            json_str( "error", mpd_status_get_error( status ), ',' );

        json_int( "volume", mpd_status_get_volume( status ), ' ' );

        fputs( "}", outbuf );
		mpd_status_free( status );
        mpd_response_finish( conn );
    }
    else
    {
        fputs( "state:{}", outbuf );  // need to put this to prevent trailing comma
    }

    fputs( "}\n", outbuf );  // end JSON output
    fclose( outbuf );

    return SUCCESS;
}

// ========= Individual MPD commands

// set mixer volume for all outputs to value between 0 and 100
int setVolume( const unsigned int vol )
{
    if( connectMPD( ) || !mpd_run_set_volume( conn, vol ) )
        return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );

    return SUCCESS;
}

// skip by the given amount
int skip( const int where )
{
    if( connectMPD( ) )
        return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );

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
        return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );

    return SUCCESS;
}

// play given song position
int play( const int position )
{
    if( connectMPD( ) )
        return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );
    
    if( position >= 0 && !mpd_run_play_pos( conn, position ) )
        return error( NOT_FOUND, NOT_FOUND_MSG, NULL );

    return SUCCESS;
}

// play given song id
int playid( const int id )
{
    if( connectMPD( ) )
        return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );

    if( id >= 0 && !mpd_run_play_id( conn, id ) )
        return error( NOT_FOUND, NOT_FOUND_MSG, NULL );

    return SUCCESS;
}

// pause / unpause playback
int pausemusic( const int position )
{
    struct mpd_status *status = NULL;

    if( connectMPD( ) || !(status = mpd_run_status( conn )) )
        error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );

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

    return SUCCESS;
}

// send list of all playlists on the server
int sendPlaylists( )
{
    struct mpd_playlist *list = NULL;
    
    if( connectMPD( ) || !mpd_send_list_playlists( conn ) )
        error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );

    // print all playlists
    fputs( "\"playlists\":[", outbuf );
    int i = 0;
    while( (list = mpd_recv_playlist( conn ) ) )
    {
        if( i )
            fputs( ",{", outbuf );
        else
            fputs( "{", outbuf );
        json_str( "name", mpd_playlist_get_path( list ), ' ' );
        fputs( "}", outbuf );
        mpd_playlist_free( list );
        i++;
    }
    fputs( "],", outbuf );
    
    mpd_response_finish( conn );
    return SUCCESS;
}

// send content of specific playlist on server
int sendPlaylist( const char *arg )
{
    struct mpd_song *song = NULL;

    if( connectMPD( ) )
        return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );

    if( arg )
    {
        if( !mpd_send_list_playlist_meta( conn, arg ) )
            return error( NOT_FOUND, NOT_FOUND_MSG, NULL );
    }
    else
    {
        if( !mpd_send_list_queue_meta( conn ) )
            return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );
    }

    // print the playlist
    fputs( "\"playlist\":[", outbuf );
    int i = 0;
    while( (song = mpd_recv_song( conn ) ) )
    {
        if( i )
            fputs( ",{", outbuf );
        else
            fputs( "{", outbuf );
        json_int( "position", i, ',' );
        json_int( "id", mpd_song_get_id( song ), ',' );
        json_str( "title", mpd_song_get_tag( song, MPD_TAG_TITLE, 0 ), ',' );
        json_str( "name", mpd_song_get_tag( song, MPD_TAG_NAME, 0 ), ',' );
        json_str( "artist", mpd_song_get_tag( song, MPD_TAG_ARTIST, 0 ), ',' );
        json_str( "track", mpd_song_get_tag( song, MPD_TAG_TRACK, 0 ), ',' );
        json_str( "album", mpd_song_get_tag( song, MPD_TAG_ALBUM, 0 ), ',' );
        json_str( "uri", mpd_song_get_uri( song ), ' ' );
        fputs( "}", outbuf );
        mpd_song_free( song );
        i++;
    }
    fputs( "],", outbuf );

    mpd_response_finish( conn );
    return SUCCESS;
}

// load the specified playlist into the queue, replacing current queue
int loadPlaylist( const char *arg )
{
    if( connectMPD( ) )
        return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );
    mpd_run_clear( conn );
    if( !mpd_run_load( conn, arg ) )
        return error( NOT_FOUND, NOT_FOUND_MSG, NULL );
#ifdef AUTOPLAY
    mpd_run_play_pos( conn, 0 );  // try to autoplay the first song
#endif
    return sendPlaylist( NULL );
}

// load part of the music directory (recursively) into the queue, replacing current queue
int loadMusic( const char *arg )
{
    if( connectMPD( ) )
        return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );
    mpd_run_clear( conn );
    mpd_search_add_db_songs( conn, false );
    mpd_search_add_any_tag_constraint( conn, MPD_OPERATOR_DEFAULT, "" );    // searches must have some constraint, this just matches everything
    if( arg && *arg != '\0' )
        mpd_search_add_uri_constraint( conn, MPD_OPERATOR_DEFAULT, arg ); // matches against full file name relative to music dir. empty arg matches everything.
        //mpd_search_add_base_constraint( conn, MPD_OPERATOR_DEFAULT, arg );  // restrict search to subdirectory of music dir, must be non-empty, error if directory does not exist
    // not allowed in _add_ calls yet (error: "incorrect arguments")
    //mpd_search_add_sort_tag( conn, MPD_TAG_ARTIST_SORT, false );    // are multiple sort tags supported?
    //mpd_search_add_sort_tag( conn, MPD_TAG_TITLE, false );
    if( !mpd_search_commit( conn ) || !mpd_response_finish( conn ) )
        return error( NOT_FOUND, NOT_FOUND_MSG, NULL );
#ifdef AUTOPLAY
    mpd_run_play_pos( conn, 0 );  // try to autoplay the first song
#endif
    return sendPlaylist( NULL );
}

// add song(s) to playlist and send new queue
int add( char *arg )
{
    if( connectMPD( ) || !mpd_command_list_begin( conn, false ) )
        return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );

    char *url;
    while( (url = strsep( &arg, "|" )) )
    {
        if( !mpd_send_add( conn, url ) )
            return error( NOT_FOUND, NOT_FOUND_MSG, NULL );
    }

    if( !mpd_command_list_end( conn ) )
        return error( NOT_FOUND, NOT_FOUND_MSG, NULL );

    mpd_response_finish( conn );
    return sendPlaylist( NULL );
}

// send password (unencrypted clear text, mostly window dressing)
int sendPassword( const char *arg )
{
    if( connectMPD( ) )
        return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );
    if( !mpd_run_password( conn, arg ) )
        return error( FORBIDDEN, FORBIDDEN_MSG, NULL );
    return SUCCESS;
}

// send some statistics
int sendStatistics( )
{
    struct mpd_stats *stat;
    char str[64];
    time_t t;

    if( connectMPD( ) || !(stat = mpd_run_stats( conn )) )
        return error( SERVER_ERROR, SERVER_ERROR_MSG, NULL );

    fputs( "\"stats\":{", outbuf );
    json_int( "artists", mpd_stats_get_number_of_artists( stat ), ',' );
    json_int( "albums", mpd_stats_get_number_of_albums( stat ), ',' );
    json_int( "songs", mpd_stats_get_number_of_songs( stat ), ',' );
    json_int( "uptime", mpd_stats_get_uptime( stat ), ',' );
    json_int( "playtime", mpd_stats_get_play_time( stat ), ',' );
    json_int( "totaltime", mpd_stats_get_db_play_time( stat ), ',' );
    t = mpd_stats_get_db_update_time( stat );
    strftime( str, 64, "%a, %d %b %Y %T %z", localtime( &t ) );
    json_str( "dbupdate", str, ' ' );
    fputs( "},", outbuf );

    mpd_stats_free( stat );

    return SUCCESS;
}

// reboot system (if priviliges allow)
int rebootSystem( const int mode )
{
#ifdef SYSTEMD
    sd_bus *bus = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;

    /* Connect to the system bus (adapted from http://0pointer.net/blog/the-new-sd-bus-api-of-systemd.html) */
    int r = sd_bus_open_system( &bus );
    if( r < 0 )
        return error( SERVER_ERROR, SERVER_ERROR_MSG, strerror( -r ) );
    r = sd_bus_call_method( bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                            "org.freedesktop.systemd1.Manager",
                            mode == RB_POWER_OFF ? "PowerOff" : "Reboot",
                            &err, NULL, NULL );
    if( r < 0 )
    {
        sd_bus_unref( bus );
        r = error( SERVER_ERROR, SERVER_ERROR_MSG, err.message );
        sd_bus_error_free( &err );
        return r;
    }
#else
    sync( );
    usleep( REBOOT_WAIT );     // wait for buffers to flush. Not done in systemctl reboot.
    reboot( mode );
#endif
    return error( SERVER_ERROR, SERVER_ERROR_MSG, "Shutdown or reboot failed" );     // not reached
}

// ========= CGI command handlers

// Parse a command
int parseCommand( char *cmd )
{
    if( strncmp( cmd, "password=", 9 ) == 0 )
    {
        // Send password
        return sendPassword( cmd+9 );
    }
    else if( *cmd == '\0' || strcmp( cmd, "state" ) == 0 )
    {
        // Send current state (current queue, current song, ...)
        // This is automatically added at the end to every successful request, so we do nothing
        return SUCCESS;
    }
    else if( strcmp( cmd, "playlists" ) == 0 )
    {
        // Send list of all playlists on server
        return sendPlaylists( );
    }
    else if( strncmp( cmd, "playlist=", 9 ) == 0 )
    {
        // Send content of given playlist
        return sendPlaylist( cmd+9 );
    }
    else if( strcmp( cmd, "queue" ) == 0 )
    {
        // Send content of current queue
        return sendPlaylist( NULL );
    }
    else if( strncmp( cmd, "load=", 5 ) == 0 )
    {
        // Load the given playlist to replace the current queue and send its content
        return loadPlaylist( cmd+5 );
    }
    else if( strncmp( cmd, "music=", 6 ) == 0 )
    {
        // Load the given music directory (recursively) to replace the current queue and send its content
        return loadMusic( cmd+6 );
    }
    else if( strcmp( cmd, "forward" ) == 0 )
    {
        // Jump to next item on playlist
        return skip( 1 );
    }
    else if( strcmp( cmd, "back" ) == 0 )
    {
        // Jump to previous item on playlist
        return skip( -1 );
    }
    else if( strncmp( cmd, "play=", 5 ) == 0 )
    {
        // Start playback at given position
        return play( strtol( cmd+5, NULL, 10 ) );
    }
    else if( strncmp( cmd, "playid=", 7 ) == 0 )
    {
        // Start playback at given id
        return playid( strtol( cmd+7, NULL, 10 ) );
    }
    else if( strncmp( cmd, "pause=", 6 ) == 0 )
    {
        // Pause playback
        return pausemusic( strtol( cmd+6, NULL, 10 ) );
    }
    else if( strncmp( cmd, "add=", 4 ) == 0 )
    {
        // Add song to queue
        return add( cmd+4 );
    }
    else if( strncmp( cmd, "volume=", 7 ) == 0 )
    {
        // Set the mixer volume
        return setVolume( strtol( cmd+7, NULL, 10 ) );
    }
    else if( strcmp( cmd, "reboot" ) == 0 )
    {
        // Reboot the system (assuming sufficient priviliges)
        return rebootSystem( RB_AUTOBOOT );
    }
    else if( strcmp( cmd, "shutdown" ) == 0 )
    {
        // Reboot the system (assuming sufficient priviliges)
        return rebootSystem( RB_POWER_OFF );
    }
    else if( strcmp( cmd, "stats" ) == 0 )
    {
        // Send some statistics
        return sendStatistics( );
    }
    else
    {
        // No idea what you want from me
        return error( BAD_REQUEST, BAD_REQUEST_MSG, "Request not understood" );
    }
}

// Handle each request in a CGI query string
int handleQuery( char *arg )
{
    char *var, *str = arg;
    int rc = 0;

    while( !rc && ((var = strsep( &str, "&" )) != NULL) )
    {
        // URL decode argument
        char *argdec = urldecode( var );
        if( argdec == NULL )
            rc = error( SERVER_ERROR, SERVER_ERROR_MSG, "Request failed" );
        if( !rc ) rc = parseCommand( argdec );
        free( argdec );
    }

    return rc;
}

// Main CGI program entry point
int cgi_main( int argc, char *argv[] )
{
    // open output buffer
    char *obuf = NULL;
    size_t obuf_size = 0;

    if( output_start( &obuf, &obuf_size ) )
    {
        outbuf = stdout;    // error allocating output buffer, use stdout directly to print error and exit
        return error( SERVER_ERROR, SERVER_ERROR_MSG, "Request failed" );
    }

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

// ========= Standalone mini HTTP/1.1 server

/* */
typedef struct {
    char *data;             // data pointer
    unsigned int max, len;  // max allocated length, current length
    unsigned int cl;        // content length (if known)
    char *version, *method, *url, *head, *body;      // request pointers
    int rnrn;               // flag if the line delimiter is \r\n (1) or \n (0)
    int fd;                 // socket associated with this request
} req;

// handle a CGI query with given query string
int handle_cgi( int fd, char *query )
{
    // open output buffer
    char *obuf = NULL;
    size_t obuf_size = 0;
    if( output_start( &obuf, &obuf_size ) )
    {
        perror( "output_start" );
        exit( EXIT_FAILURE );
    }

    int rc = 0;
    if( !rc ) rc = handleQuery( query );

    // write output
    if( !rc ) rc = output_end( );
    char tmp[256];
    int tmp_size = snprintf( tmp, 256, rc ? "HTTP/1.1 500 Server error\r\nContent-Length: %d\r\n" : "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n", obuf_size );
    write( fd, tmp, tmp_size );
    write( fd, obuf, obuf_size );   // either error or output_end will have closed output buffer stream

    // clean up
    free( obuf );
    return 0;
}

// parse request headers
int handle_head( req *c )
{
    // did we finish reading the headers?
    char *tmp = strstr( c->head, c->rnrn ? "\r\n\r\n" : "\n\n" );
    if( tmp == NULL ) return WAIT_FOR_DATA;         // need more data
    c->body = tmp + 2*(1 + c->rnrn);    // this is where the body starts (2 or 4 forward)
    c->cl += c->body - c->data;         // length of the headers alone

#ifdef DEBUG
    printf( "Headers: %s\n", c->head );
#endif
    
    // hooray! we have headers, parse them
    char *p = c->head;
    while( *p )
    {
        // find end of current header and zero terminate it
        tmp = strstr( p, c->rnrn ? "\r\n" : "\n" );
        if( tmp )
        {
            *tmp = '\0';
            tmp++;
            if( c->rnrn )
            {
                *tmp = '\0';
                tmp++;
            }
        }
        // check for Content-Length header, currently the only one we care about
        if( strncmp( p, "Content-Length: ", 16 ) == 0 )
            c->cl += strtol( p+16, NULL, 10 );
        p = tmp;
    }
    
    return 0;
}

// parse and handle request body
int handle_body( req *c )
{
    if( c->len < c->cl ) return WAIT_FOR_DATA; // request is still expecting data
#ifdef DEBUG
    printf( "Body: %s\n", c->body );
#endif

    // check what to do with this requst
    if( strncmp( c->url, "/cgi-bin/ir.cgi", 15 ) == 0 )
    {
        char *query = c->url+15;
        if( *query == '?' ) query++;

        if( strcmp( c->method, "POST" ) == 0 )
            query = c->body;

        handle_cgi( c->fd, query );
    }
    //else if()
    //{
    //
    //}
    else
    {
        write( c->fd, "HTTP/1.1 404 Not found\r\nContent-Length: 15\r\n\r\n404 - Not found", 62 );
    }

    // remove handled data from request buffer, ready for next request (allowing pipelining, keep-alive)
    const unsigned int rem = c->len-c->cl;
    if( rem > 0 )
        memmove( c->data, c->data+c->cl, rem );
    c->len -= c->cl;
    c->version = c->method = c->url = c->head = c->body = NULL;
    c->cl = 0;

    return 0;
}

// attempt to handle a request (if it is ready to be handled)
int handle_request( req *c )
{
    // Try to read the request line
    c->rnrn = 1;
    char *tmp = strstr( c->data, "\r\n" );
    if( tmp == NULL )
    {
        tmp = strstr( c->data, "\n" );
        if( tmp == NULL ) return WAIT_FOR_DATA;     // we need more data
        c->rnrn = 0;
    }

    // zero terminate request line and mark header position
    *tmp = '\0';
    c->head = tmp+1+c->rnrn;  // where the headers begin

    tmp = c->data;
#ifdef DEBUG
    printf( "Request:\n%s\n", tmp );
#endif
    // parse request line: version
    tmp += strspn( c->data, " \t" ); // skip whitespace
    c->version = tmp;
    tmp += strcspn( tmp, " \t" );
    if( *tmp )
    {
        *tmp = '\0';
        tmp++;
    }
    // method
    tmp += strspn( tmp, " \t" );
    c->method = tmp;
    tmp += strcspn( tmp, " \t" );
    if( *tmp )
    {
        *tmp = '\0';
        tmp++;
    }
    // url
    tmp += strspn( tmp, " \t" );
    c->url = tmp;

#ifdef DEBUG
    printf( "Version: %s\tMethod: %s\tURL: %s\n", c->version, c->method, c->url );
#endif

    return 0;
}

// find out where in the rquest phase this request is and try to handle new data accordingly
int handle_data( req *c )
{
    int rc;
    bool cont;

    do {
#ifdef DEBUG
        printf( "Data:\n%s\n", c->data );
#endif
        cont = false;

        // request is new, waiting for request line
        if( !c->head )
        {
            rc = handle_request( c );
            if( rc ) return rc;
        }

        // request is waiting for headers to arrive
        if( c->head )
        {
            int rc = handle_head( c );
            if( rc ) return rc;
        }

        // request is waiting for body to arrive
        if( c->body )
        {
            int rc = handle_body( c );
            if( rc ) return rc;
            cont = true;    // repeat with (potentially) next pipelined request
        }
    } while( cont );

    return 0;
}

// read from a socket and store data in request
int read_from_client( req *c )
{
    // increase buffer if needed
    int len = c->max - c->len - 1;
    if( len < 128 )
    {
        if( !(c->data = realloc( c->data, c->max+4096 )) )
        {
            perror( "realloc" );
            exit( EXIT_FAILURE );
        }
        c->max += 4096;
        len += 4096;
    }

    // read data
    int nbytes = read( c->fd, c->data+c->len, len );
    if( nbytes == 0 )
        return -1;  // nothing to read from this socket, must be closed => request finished
    else if( nbytes < 0 )
    {
        perror( "read" );
        exit( EXIT_FAILURE );
    }
    c->len += nbytes;
    c->data[c->len] = '\0';

    // try to handle the request
    return handle_data( c );
}

// Main HTTP server program entry point (adapted from https://www.gnu.org/software/libc/manual/html_node/Waiting-for-I_002fO.html#Waiting-for-I_002fO)
int server_main( int argc, char *argv[] )
{
    int serverSocket = 0;
    struct sockaddr_in serverAddr = { 0 };
    struct sockaddr_storage serverStorage = { 0 };
    socklen_t addr_size = sizeof(serverStorage);

    serverSocket = socket( PF_INET, SOCK_STREAM, 0 );
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons( SERVER_PORT );
    inet_pton( AF_INET, SERVER_IP, &serverAddr.sin_addr.s_addr );
    bind( serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr) );

    if( listen( serverSocket, 5 ) < 0 )
    {
        perror( "listen" );
        exit( EXIT_FAILURE );
    }

    // initialize active sockets set
    req reqs[FD_SETSIZE] = { 0 };  // data buffer read from clients
    fd_set active_fd_set, read_fd_set;
    FD_ZERO( &active_fd_set );
    FD_SET( serverSocket, &active_fd_set );

    while( true )
    {
        // wait for input
        read_fd_set = active_fd_set;
        if( select( FD_SETSIZE, &read_fd_set, NULL, NULL, NULL ) < 0 )
        {
            perror( "select" );
            exit( EXIT_FAILURE );
        }

        // process input for active sockets
        for( int i = 0; i < FD_SETSIZE; i++ )
            if( FD_ISSET( i, &read_fd_set ) )
            {
                if( i == serverSocket )
                {
                    // Connection request on original socket
                    addr_size = sizeof(serverAddr);
                    const int new = accept( serverSocket, (struct sockaddr *) &serverAddr, &addr_size );
                    if( new < 0 || new >= FD_SETSIZE )
                    {
                        perror( "accept" );
                        exit( EXIT_FAILURE );
                    }
                    FD_SET( new, &active_fd_set );

                    // allocate/clear some memory for reading request
                    if( !reqs[new].data )
                    {
                        if( !(reqs[new].data = malloc( 4096 )) )
                        {
                            perror( "malloc" );
                            exit( EXIT_FAILURE );
                        }
                        reqs[new].max = 4096;
                    }
                    reqs[new].len = 0;
                    reqs[new].cl = 0;
                    reqs[new].data[0] = '\0';
                    reqs[new].fd = new;
                    reqs[new].version = reqs[new].method = reqs[new].url = reqs[new].head = reqs[new].body = NULL;
                }
                else
                {
                    // data arriving from active socket
                    if( read_from_client( &reqs[i] ) < 0 )
                    {
                        // close socket
                        close( i );
                        FD_CLR( i, &active_fd_set );

                        // free previous request data
                        free( reqs[i].data );
                        bzero( &reqs[i], sizeof(req) );
                    }
                }
            }
    }

    disconnectMPD( );
    return SUCCESS;
}
/*

int server_main( int argc, char *argv[] )
{
    return SUCCESS;
}
*/
// select the right main function
int main( int argc, char *argv[] )
{
#ifdef CGI
    return cgi_main( argc, argv );
#elif SERVER
    return server_main( argc, argv );
#endif
    return SUCCESS;
}
