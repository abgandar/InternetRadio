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
 * ir-server.c
 *
 * Standalone HTTP server to connect to the mpd daemon and communicate various XML HTTP
 * requests from the client.
 *
 * Simply execute the ir-server binary.
 *
 */

#define __USE_POSIX
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <stdbool.h>
#include <time.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "ir-common.h"
#include "ir-server-content.h"
#include "ir-server-mime.h"

// indicator if the main loop is still running (used for signalling)
static bool running = true;

// some return codes
typedef enum retcode_enum { CLOSE_SOCKET = -1, SUCCESS = 0, WAIT_FOR_DATA = 1 } retcode;

// some HTTP status codes
typedef enum statuscode_enum {
    HTTP_OK = 200,
    HTTP_NOT_MODIFIED = 304,
    HTTP_BAD_REQUEST = 400,
    HTTP_NOT_FOUND = 404,
    HTTP_NOT_ALLOWED = 405,
    HTTP_TOO_LARGE = 413,
    HTTP_SERVER_ERROR = 500,
    HTTP_NOT_IMPLEMENTED = 501,
    HTTP_SERVICE_UNAVAILABLE = 503
} statuscode;

// request method
typedef enum method_enum { M_UNKNOWN, M_OPTIONS, M_GET, M_HEAD, M_POST, M_PUT, M_DELETE, M_TRACE, M_CONNECT } method;

// request version
typedef enum version_enum { V_UNKNOWN, V_10, V_11 } version;

// request flags (bitfield, assign powers of 2!)
typedef enum flags_enum { FL_NONE = 0, FL_CRLF = 1, FL_CHUNKED = 2 } flags;

// request state
typedef enum state_enum { STATE_NEW, STATE_HEAD, STATE_BODY, STATE_TAIL, STATE_READY, STATE_FINISH } state;

// some human readable equivalents to HTTP status codes above (must be sorted by code except for last!)
typedef struct response_struct { const unsigned int code; const char *msg; } response;
static const response responses[] = {
    { HTTP_OK, "OK" },
    { HTTP_NOT_MODIFIED, "Not modified" },
    { HTTP_BAD_REQUEST, "Bad request" },
    { HTTP_NOT_FOUND, "Not found" },
    { HTTP_NOT_ALLOWED, "Method not allowed" },
    { HTTP_TOO_LARGE, "Payload too large" },
    { HTTP_SERVER_ERROR, "Server error" },
    { HTTP_NOT_IMPLEMENTED, "Not implemented" },
    { HTTP_SERVICE_UNAVAILABLE, "Service unavailable" },
    { 0, NULL }
};

// an active request
typedef struct request_struct {
    int fd;                 // socket associated with this request
    char *data;             // data buffer pointer
    unsigned int max, len;  // max allocated length, current length
    unsigned int rl, cl;    // total request length parsed, total body content length
    char *version, *method, *url, *head, *body, *tail;      // request pointers into data buffer
    state s;                // state of this request
    flags f;                // flags for this request
    version v;              // HTTP version of request
    method m;               // enumerated method
} req;

// allocate memory and set everything to zero
inline void INIT_REQ( req *c, const int fd )
{
    if( c->data ) free( c->data );  // should never happen but just to be safe
    bzero( c, sizeof(req) );

    if( !(c->data = malloc( 4096 )) )
    {
        perror( "malloc" );
        exit( EXIT_FAILURE );
    }
    c->data[0] = '\0';
    c->max = 4096;
    c->fd = fd;
}

// free request memory
inline void FREE_REQ( req *c )
{
    if( c->data ) free( c->data );
    c->data = NULL; c->max = 0;
}

// reset a request keeping its data and buffer untouched
inline void RESET_REQ( req *c )
{
    c->rl = c->cl = 0;
    c->version = c->method = c->url = c->head = c->body = c->tail = NULL;
    c->s = c->f = c->v = c->m = 0;
}

// signal handler
void handle_signal( const int sig )
{
    if( sig == SIGTERM || sig == SIGINT )
    {
        running = false;
        debug_printf( "===> Received signal\n" );
    }
}

// guess a mime type for a filename
const char* get_mime( const char* fn )
{
    const char const* end = fn+strlen( fn )-1;

    for( unsigned int i = 0; mimetypes[i].ext; i++ )
    {
        const char *p, *q;
        for( p = end, q = mimetypes[i].ext; *q && p >= fn && *p == *q; p--, q++ );
        if( *q == '\0' )
            return mimetypes[i].mime;
    }

    return "application/octet-stream";  // don't know, fall back to this
}

// get human readable response from code
const char* get_response( const unsigned int code )
{
    unsigned int i;
    for( i = 0; responses[i].code && responses[i].code < code; i++ );
    if( responses[i].code == code )
        return responses[i].msg;
    else
        return "Unknown";  // response was not found
}

// get first matching header value from the request (or NULL if not found)
// name must be of the form "Date: " (including colon and space)
const char* get_header_field( const req *c, const char* name )
{
    char *p = c->head;
    unsigned int len = strlen( name );
    
    // check in headers
    if( c->head && *c->head )
        for( p = c->head; *p; p += strlen( p )+1+(c->f & FL_CRLF) )
            if( strncmp( p, name, len ) == 0 )
                return p+len;
    
    // check in trailers
    if( c->tail && *c->tail )
        for( p = c->tail; *p; p += strlen( p )+1+(c->f & FL_CRLF) )
            if( strncmp( p, name, len ) == 0 )
                return p+len;
    
    return NULL;
}

// write a response with given body and headers (must include the initial HTTP response header)
// automatically adds Date header and respects HEAD requests
// if body is non-NULL, it is sent as a string with appropriate Content-Length header
// if body is NULL, and bodylen is non-null, the value is sent, expecting caller to send the data on its own
void write_response( const req *c, const unsigned int code, const char* headers, const char* body, unsigned int bodylen )
{
    // autodetermine length
    if( body != NULL && bodylen == 0 )
        bodylen = strlen( body );

    // get current time
    char str[64];
    const time_t t = time( NULL );
    strftime( str, 64, "%a, %d %b %Y %T %z", localtime( &t ) );

    // prepare additional headers
    struct iovec iov[2];
    iov[0].iov_len = asprintf( (char** restrict) &(iov[0].iov_base),
                               "HTTP/1.%c %u %s\r\n" EXTRA_HEADER "%sContent-Length: %u\r\nDate: %s\r\n\r\n",
                               c->v == V_10 ? '0' : '1', code, get_response( code ), headers ? headers : "", bodylen, str );

    // write everything using a single call
    if( body && c->m != M_HEAD && bodylen > 0 )
    {
        iov[1].iov_base = (char*)body;
        iov[1].iov_len = bodylen;
        writev( c->fd, iov, 2 );
    }
    else
        write( c->fd, iov[0].iov_base, iov[0].iov_len );

    free( iov[0].iov_base );
}

// handle a CGI query
int handle_cgi( const req *c )
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
    char *query = c->url+15;
    if( *query == '?' ) query++;
    if( c->m == M_POST )
        query = c->body;    // rely on body being null terminated
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

// handle a file query for an embedded file
int handle_embedded_file( const req *c )
{
    unsigned int i;
    for( i = 0; contents[i].url && strcmp( c->url, contents[i].url ); i++ );

    if( contents[i].url )
    {
#ifdef TIMESTAMP
        const char *inm = get_header_field( c, "If-None-Match: " );
        if( inm && strcmp( inm, TIMESTAMP ) == 0 )
        {
            write_response( c, HTTP_NOT_MODIFIED, contents[i].headers, NULL, 0 );
            return SUCCESS;
        }
#endif
        write_response( c, HTTP_OK, contents[i].headers, contents[i].body, contents[i].len );
        return SUCCESS;
    }

    return HTTP_NOT_FOUND;
}

// handle a disk file query
int handle_file( const req *c )
{
    if( strstr( c->url, ".." ) != NULL )
        return HTTP_NOT_FOUND;

    const int len_WWW_DIR = strlen( WWW_DIR ), len_url = strlen( c->url ), len_DIR_INDEX = strlen( DIR_INDEX );
    if( len_WWW_DIR+len_url+len_DIR_INDEX >= PATH_MAX )
        return HTTP_NOT_FOUND;

    char fn[PATH_MAX];
    memcpy( fn, WWW_DIR, len_WWW_DIR );
    memcpy( fn+len_WWW_DIR, c->url, len_url );
    fn[len_WWW_DIR+len_url] = '\0';
    if( len_url == 0 || c->url[len_url-1] == '/' )
    {
        memcpy( fn+len_WWW_DIR+len_url, DIR_INDEX, len_DIR_INDEX );
        fn[len_WWW_DIR+len_url+len_DIR_INDEX] = '\0';
    }
    debug_printf( "===> Trying to open file: %s\n", fn );

    // open file
    int fd = open( fn, O_RDONLY );
    if( fd < 0 )
        return HTTP_NOT_FOUND;

    // file statistics
    struct stat sb;
    if( fstat( fd, &sb ) )
    {
        write_response( c, HTTP_SERVER_ERROR, NULL, "500 - Server error", 0 );
        close( fd );
        return SUCCESS;
    }
    debug_printf( "===> File size, modification time: %u, %ld\n", sb.st_size, sb.st_mtim.tv_sec );

    // write headers
    char *str;
    asprintf( &str, "ETag: \"%ld\"\r\nContent-Type: %s\r\n", sb.st_mtim.tv_sec, get_mime( fn ) );

    // check ETag
    const char *inm = get_header_field( c, "If-None-Match: " );
    char *last = NULL;
    if( inm && (strtol( inm+1, &last, 10 ) == sb.st_mtim.tv_sec) && last && (*last == '"') )
    {
        write_response( c, HTTP_NOT_MODIFIED, str, NULL, 0 );
        free( str );
        close( fd );
        return SUCCESS;
    }

    // write response
    write_response( c, HTTP_OK, str, NULL, sb.st_size );
    free( str );
    if( c->m != M_HEAD )
        sendfile( c->fd, fd, NULL, sb.st_size );
    close( fd );

    return SUCCESS;
}

// finish request after had been handled
int finish_request( req *c )
{
    // remove handled data from request buffer, ready for next request (allowing pipelining, keep-alive)
    int rem = c->len - c->rl;    // should never underflow but just to be sure
    debug_printf( "===> Finish: %d bytes left (%d)\n", rem, c->rl );
    if( rem > 0 )
        memmove( c->data, c->data+c->rl, rem );
    else if( rem < 0 )
        rem = 0;
    c->data[rem] = '\0';
    c->len = rem;
    RESET_REQ( c );

    return SUCCESS;
}

// handle request after it was completely read
int handle_request( req *c )
{
    if( c->m != M_GET && c->m != M_POST && c->m != M_HEAD )
        write_response( c, HTTP_NOT_ALLOWED, NULL, "405 - Not allowed", 0 );
    else
    {
        // check what to do with this requst
        if( strncmp( c->url, "/cgi-bin/ir.cgi", 15 ) == 0 )
            handle_cgi( c );
        else if( handle_embedded_file( c ) == SUCCESS )
            ((void)0);  // do nothing
        else if( handle_file( c ) )
            write_response( c, HTTP_NOT_FOUND, NULL, "404 - Not found", 0 );
    }

    c->s = STATE_FINISH;
    return SUCCESS;
}

// parse request trailer
int read_tail( req *c )
{
    // did we finish reading the tailers?
    char *tmp = strstr( c->tail, (c->f & FL_CRLF) ? "\r\n\r\n" : "\n\n" );
    if( tmp == NULL )
    {
        // in case no tailers were sent at all there's only one empty line
        if( strncmp( c->tail, (c->f & FL_CRLF) ? "\r\n" : "\n", 1+(c->f & FL_CRLF) ) == 0 )
        {
            tmp = c->tail;
            c->rl = tmp - c->data + 1 + (c->f & FL_CRLF);
        }
        else
            return WAIT_FOR_DATA;           // need more data
    }
    else
        c->rl = tmp - c->data + 2 + 2*(c->f & FL_CRLF);

    debug_printf( "===> Trailers:\n" );

    // hooray! we have trailers, parse them
    char *p = c->tail;
    while( p )
    {
        // find end of current header and zero terminate it => p points to current header line
        tmp = strstr( p, (c->f & FL_CRLF) ? "\r\n" : "\n" );
        if( tmp )
        {
            tmp[0] = '\0';
            if( c->f & FL_CRLF )
                tmp[1] = '\0';
        }
        if( !*p ) break; // found empty header => done reading headers
        debug_printf( "     %s\n", p );
        // point p to next header
        p = tmp + 1 + (c->f & FL_CRLF);
    }
    
    c->s = STATE_READY;
    return SUCCESS;
}

// parse request body
int read_body( req *c )
{
    if( c->f & FL_CHUNKED )
    {
        // Read as many chunks as there are
        while( true )
        {
            char *tmp = strstr( c->data + c->rl, (c->f & FL_CRLF) ? "\r\n" : "\n" );
            if( !tmp ) return WAIT_FOR_DATA;
            // get next chunk length
            tmp += 1 + (c->f & FL_CRLF);
            char *perr;
            unsigned int chunklen = strtol( c->data + c->rl, &perr, 16 );
            if( *perr != '\n' && *perr != '\r' && *perr != ';' )
            {
                write_response( c, HTTP_BAD_REQUEST, NULL, "400 - Bad request", 0 );
                return CLOSE_SOCKET;
            }
            // if this is the last chunk, eat the line and finish request
            if( chunklen == 0 )
            {
                c->rl = tmp - c->data;
                break;
            }
            // wait till the entire chunk is here
            if( c->len < (tmp - c->data + chunklen + 1 + (c->f & FL_CRLF)) )
                return WAIT_FOR_DATA;
            debug_printf( "===> Reading chunk size %d\n", chunklen );
            // copy the chunk back to the end of the body
            memmove( c->body + c->cl, tmp, chunklen );
            c->cl += chunklen;
            c->rl = tmp - c->data + chunklen + 1 + (c->f & FL_CRLF);   // skip the terminating CRLF
        }
        c->tail = c->data+c->rl;
        c->s = STATE_TAIL;
    }
    else
    {
        // Normal read of given content length
        if( c->len < c->rl ) return WAIT_FOR_DATA;
        c->s = STATE_READY;
    }
    debug_printf( "===> Body (%d bytes):\n%.*s\n", c->cl, c->cl, c->body );

    return SUCCESS;
}

// parse request headers
int read_head( req *c )
{
    // did we finish reading the headers?
    char *tmp = strstr( c->head, (c->f & FL_CRLF) ? "\r\n\r\n" : "\n\n" );
    if( tmp == NULL )
    {
        // in case no headers were sent at all there's only one empty line
        if( strncmp( c->head, (c->f & FL_CRLF) ? "\r\n" : "\n", 1+(c->f & FL_CRLF) ) == 0 )
        {
            tmp = c->head;
            c->body = tmp + 1 + (c->f & FL_CRLF);    // this is where the body starts (1 or 2 forward)
        }
        else
            return WAIT_FOR_DATA;           // need more data
    }
    else
        c->body = tmp + 2*(1 + (c->f & FL_CRLF));      // this is where the body starts (2 or 4 forward)
    c->rl = c->body - c->data;  // request length so far (not including body)

    debug_printf( "===> Headers:\n" );

    // hooray! we have headers, parse them
    char *p = c->head;
    while( p )
    {
        // find end of current header and zero terminate it => p points to current header line
        tmp = strstr( p, (c->f & FL_CRLF) ? "\r\n" : "\n" );
        if( tmp )
        {
            tmp[0] = '\0';
            if( c->f & FL_CRLF )
                tmp[1] = '\0';
        }
        if( !*p ) break; // found empty header => done reading headers
        debug_printf( "     %s\n", p );
        // check for known headers we care about (currently only Content-Length)
        if( strncmp( p, "Content-Length: ", 16 ) == 0 )
        {
            char *perr;
            unsigned int cl = strtol( p+16, &perr, 10 );
            if( *perr != '\0' || cl < 0 || (c->cl > 0 && cl != c->cl) )
            {
                write_response( c, HTTP_BAD_REQUEST, NULL, "400 - Bad request", 0 );
                return CLOSE_SOCKET;
            }
            if( cl > MAX_REQ_LEN )
            {
                write_response( c, HTTP_TOO_LARGE, NULL, "413 - Payload too large", 0 );
                return CLOSE_SOCKET;
            }
            c->cl = cl;
            c->rl += cl;
            debug_printf( "===> Content-Length: %d (%d total)\n", c->cl, c->rl );
        }
        else if( strncmp( p, "Transfer-Encoding: ", 19 ) == 0 )
        {
            if( strncmp( p+19, "chunked", 7 ) != 0 )
            {
                // c.f. http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html#sec10.5.2
                write_response( c, HTTP_NOT_IMPLEMENTED, NULL, "501 - requested Transfer-Encoding not implemented", 0 );
                return CLOSE_SOCKET;
            }
            c->f |= FL_CHUNKED;
        }
        // point p to next header
        p = tmp + 1 + (c->f & FL_CRLF);
    }

    c->s = STATE_BODY;
    return SUCCESS;
}

// parse the request line if it is available
int read_request( req *c )
{
    char* data = c->data;

    // Optionally ignore an empty line at beginning of request (rfc7230, 3.5)
    if( data[0] == '\r' && data[1] == '\n' )
        data+=2;
    else if( data[0] == '\n' )
        data++;
    
    // Try to read the request line
    c->f |= FL_CRLF;
    char *tmp = strstr( data, "\r\n" );
    if( tmp == NULL )
    {
        tmp = strstr( data, "\n" );
        if( tmp == NULL ) return WAIT_FOR_DATA;     // we need more data
        c->f &= ~FL_CRLF;
    }

    // zero terminate request line and mark header position
    *tmp = '\0';
    c->head = tmp+1+(c->f & FL_CRLF);  // where the headers begin

    // parse request line
    tmp = data;
    debug_printf( "===> Request:\n%s\n", tmp );
    // method
    tmp += strspn( data, " \t" );
    c->method = tmp;
    tmp += strcspn( tmp, " \t" );
    if( *tmp )
    {
        *tmp = '\0';
        tmp++;
    }
    // uri
    tmp += strspn( tmp, " \t" );
    c->url = tmp;
    tmp += strcspn( tmp, " \t" );
    if( *tmp )
    {
        *tmp = '\0';
        tmp++;
    }
    // version
    tmp += strspn( tmp, " \t" );
    c->version = tmp;

    // identify method
    if( strcmp( c->method, "GET" ) == 0 )
        c->m = M_GET;
    else if( strcmp( c->method, "POST" ) == 0 )
        c->m = M_POST;
    else if( strcmp( c->method, "HEAD" ) == 0 )
        c->m = M_HEAD;
    else if( strcmp( c->method, "OPTIONS" ) == 0 )
        c->m = M_OPTIONS;
    else if( strcmp( c->method, "PUT" ) == 0 )
        c->m = M_PUT;
    else if( strcmp( c->method, "DELETE" ) == 0 )
        c->m = M_DELETE;
    else if( strcmp( c->method, "TRACE" ) == 0 )
        c->m = M_TRACE;
    else if( strcmp( c->method, "CONNECT" ) == 0 )
        c->m = M_CONNECT;
    else
        c->m = M_UNKNOWN;

    // identify version
    if( strcmp( c->version, "HTTP/1.1" ) == 0 )
        c->v = V_11;
    else if( strcmp( c->version, "HTTP/1.0" ) == 0 )
        c->v = V_10;
    else
        c->v = V_UNKNOWN;

    debug_printf( "===> Version: %s\tMethod: %s\tURL: %s\n", c->version, c->method, c->url );

    // does it look like a valid request?
    if( c->v == V_UNKNOWN || c->m == M_UNKNOWN )
    {
        write_response( c, HTTP_BAD_REQUEST, NULL, "400 - Bad request", 0 );
        return CLOSE_SOCKET;  // garbage request received, drop this connection
    }

    c->s = STATE_HEAD;
    return SUCCESS;
}

// find out where in the request phase this request is and try to handle new data accordingly
int parse_data( req *c )
{
    int rc;
    char *pcc, cc;

    while( true )
        switch( c->s )
        {
            case STATE_NEW:
                rc = read_request( c );
                if( rc ) return rc;
                break;

            case STATE_HEAD:
                rc = read_head( c );
                if( rc ) return rc;
                break;

            case STATE_BODY:
                rc = read_body( c );
                if( rc ) return rc;
                break;

            case STATE_TAIL:
                rc = read_tail( c );
                if( rc ) return rc;
                break;

            case STATE_READY:
                // zero terminate message body without overwriting start of next request
                // works because in chunked encoding there's space between body and trailers
                // from concatenating chunks, and without encoding there is nothing after
                // the body except for the null terminating data[] or the next request
                pcc = c->body+c->cl;
                cc = *pcc;
                *pcc = '\0';
                rc = handle_request( c );
                *pcc = cc;
                if( rc ) return rc;
                break;

            case STATE_FINISH:
                rc = finish_request( c );
                if( rc ) return rc;
                break;
        }

    return SUCCESS;
}

// read from a socket and store data in request
int read_from_client( req *c )
{
    // speculatively increase buffer if needed to avoid short reads
    int len = c->max - c->len - 1;
    if( len < 128 )
    {
        if( c->max > MAX_REQ_LEN )
        {
            write_response( c, HTTP_TOO_LARGE, NULL, "413 - Payload too large", 0 );
            return CLOSE_SOCKET;  // request is finished
        }
        if( !(c->data = realloc( c->data, c->max+4096 )) )
        {
            perror( "realloc" );
            exit( EXIT_FAILURE );
        }
        c->max += 4096;
        len += 4096;
    }

    // read data
    const int nbytes = read( c->fd, c->data+c->len, len );
    if( nbytes == 0 )
        return CLOSE_SOCKET;  // nothing to read from this socket, must be closed => request finished
    else if( nbytes < 0 )
    {
        perror( "read" );
        exit( EXIT_FAILURE );
    }
    c->len += nbytes;
    c->data[c->len] = '\0';

    // try to parse the new data
    return parse_data( c );
}

// Main HTTP server program entry point (adapted from https://www.gnu.org/software/libc/manual/html_node/Waiting-for-I_002fO.html#Waiting-for-I_002fO)
int main( int argc, char *argv[] )
{
    // set up environment
    setenv( "TZ", "GMT", true );
    setlocale( LC_ALL, "C" );

    // connect signal handlers
    struct sigaction sa_new;
    sa_new.sa_handler = handle_signal;
    sigemptyset( &sa_new.sa_mask );
    sa_new.sa_flags = 0;
    sigaction( SIGINT, &sa_new, NULL );
    sigaction( SIGTERM, &sa_new, NULL );
    sa_new.sa_handler = SIG_IGN;
    sigaction( SIGPIPE, &sa_new, NULL );

    // block signals temporarily (re-enabled only in pselect)
    sigset_t sset_disabled, sset_enabled;
    sigemptyset( &sset_disabled );
    sigaddset( &sset_disabled, SIGINT );
    sigaddset( &sset_disabled, SIGTERM );
    sigprocmask( SIG_BLOCK, &sset_disabled, &sset_enabled );
    sigdelset( &sset_enabled, SIGINT );
    sigdelset( &sset_enabled, SIGTERM );

    // get and set up server socket
    int serverSocket;
    if( !(serverSocket = socket( PF_INET, SOCK_STREAM, 0 )) )
    {
        perror( "socket" );
        exit( EXIT_FAILURE );
    }
    int yes = 1;
    setsockopt( serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int) );

    // bind and listen on correct port and IP address
    struct sockaddr_in serverAddr = { 0 };
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons( SERVER_PORT );
    inet_pton( AF_INET, SERVER_IP, &serverAddr.sin_addr.s_addr );
    if( bind( serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr) ) )
    {
        perror( "bind" );
        exit( EXIT_FAILURE );
    }
    if( listen( serverSocket, 10 ) < 0 )
    {
        perror( "listen" );
        exit( EXIT_FAILURE );
    }

#ifdef UNPRIV_USER
    // drop privileges now that init is done
    if( getuid( ) == 0 )
    {
        const struct passwd *pwd = getpwnam( UNPRIV_USER );
        if( pwd == NULL )
        {
            perror( "getpwnam" );
            exit( EXIT_FAILURE );
        }
        if( initgroups( UNPRIV_USER, pwd->pw_gid ) != 0 )
        {
            perror( "initgroups" );
            exit( EXIT_FAILURE );
        }
        if( setuid( pwd->pw_uid ) != 0 )
        {
            perror( "setuid" );
            exit( EXIT_FAILURE );
        }
    }
#endif

    // initialize poll structures
    req reqs[MAX_CONNECTIONS] = { 0 };
    struct pollfd fds[MAX_CONNECTIONS+1];
    for( unsigned int i = 0; i < MAX_CONNECTIONS+1; i++ )
    {
        fds[i].fd = -1;
        fds[i].events = POLLIN;
    }
    fds[MAX_CONNECTIONS].fd = serverSocket;

    // main loop (exited only via signal handler)
    while( running )
    {
        // wait for input
        if( ppoll( fds, MAX_CONNECTIONS, NULL, &sset_enabled ) < 0 )
        {
            if( errno == EINTR ) continue;  // ignore interrupted system calls
            perror( "ppoll" );
            exit( EXIT_FAILURE );
        }

        // process input for active sockets
        for( unsigned int i = 0; i < MAX_CONNECTIONS; i++ )
            if( fds[i].revents & POLLIN )
            {
                if( fds[i].fd == serverSocket )
                {
                    // Connection request on original socket
                    const int new = accept( serverSocket, NULL, NULL );
                    if( new < 0 )
                    {
                        perror( "accept" );
                        exit( EXIT_FAILURE );
                    }

                    // find free spot
                    unsigned int j;
                    for( j = 0; j < MAX_CONNECTIONS && fds[j].fd >= 0; j++ );

                    // are there any free connection slots?
                    if( j == MAX_CONNECTIONS )
                    {
                        // can't handle any more clients. Client will have to retry later.
                        write( new, "HTTP/1.1 503 Service unavailable\r\nContent-Length: 37\r\n\r\n503 - Service temporarily unavailable", 94 );
                        shutdown( new, SHUT_RDWR );
                        close( new );
                        debug_printf( "===> Dropped connection\n" );
                        continue;
                    }

                    // initialize request and add to watchlist
                    fds[j].fd = new;
                    INIT_REQ( &reqs[j], new );

                    debug_printf( "===> New connection\n" );
                }
                else
                {
                    // data arriving from active socket
                    if( read_from_client( &reqs[i] ) < 0 )
                    {
                        // close socket
                        shutdown( fds[i].fd, SHUT_RDWR );
                        close( fds[i].fd );
                        fds[i].fd = -1;

                        // free request data
                        FREE_REQ( &reqs[i] );
                        debug_printf( "===> Closed connection\n" );
                    }
                }
            }
            else if( fds[i].revents & (POLLERR | POLLHUP | POLLNVAL) )
            {
                // something went wrong with this socket, close it
                shutdown( fds[i].fd, SHUT_RDWR );
                close( fds[i].fd );
                if( fds[i].fd == serverSocket )
                    running = false;
                fds[i].fd = -1;

                // free request data
                FREE_REQ( &reqs[i] );
                debug_printf( "===> Closed connection\n" );
            }
    }

    debug_printf( "===> Exiting\n" );
    disconnectMPD( );
    close( serverSocket );
    return SUCCESS;
}

