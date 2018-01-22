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
 * Standalone HTTP/1.1 server for serving content from disk, embedded or special cgi handlers.
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

#include "config.h"
#include "ir-server.h"
#include "ir-server-mime.h"
#include "ir-server-content.h"

// indicator if the main loop is still running (used for signalling)
static bool running = true;

// some human readable equivalents to HTTP status codes above (must be sorted by code except for last!)
struct response_struct { const unsigned int code; const char *msg; };
static const struct response_struct responses[] = {
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
    while( c->wb )
    {
        struct wbchain_struct *p = c->wb->next;
        free( c->wb );
        c->wb = p;
    }
}

// reset a request keeping its data and buffer untouched
inline void RESET_REQ( req *c )
{
    c->rl = c->cl = 0;
    c->version = c->method = c->url = c->query = c->head = c->body = c->tail = NULL;
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

// get matching header value from the request without leading whitespace, skipping skip entries (or NULL if not found)
// name must be of the form "Date:" (including colon)
const char* get_header_field( const req *c, const char* name, unsigned int skip )
{
    char *p, *end;
    unsigned int len = strlen( name );
    
    // check in headers (if already available)
    if( c->head && c->body )
    {
        p = c->head;
        end = c->body;
        while( p < end )
        {
            if( strncasecmp( p, name, len ) == 0 )
            {
                if( skip == 0 )
                    return p+len+strspn( p+len, " \t");
                else
                    skip--;
            }
            for( p += strlen( p )+1; p < end && !*p; p++ );     // skip NULs up to next header
        }
    }

    // check in trailers
    if( c->tail )
    {
        p = c->tail;
        end = c->data+c->rl;    // trailers end at the end of the message length
        while( p < end )
        {
            if( strncasecmp( p, name, len ) == 0 )
            {
                if( skip == 0 )
                    return p+len+strspn( p+len, " \t");
                else
                    skip--;
            }
            for( p += strlen( p )+1; p < end && !*p; p++ );     // skip NULs up to next header
        }
    }

    return NULL;
}

// try to write the vector I/O directly, and if that does not succeed append it to the internal write buffer
// flags is an array of the same length as iov containing memory specifiers for each buffer
// MEM_KEEP means the buffer is always valid, MEM_FREE means the buffer will be free()ed after use, and
// MEM_COPY means the buffer will be copied internally. If NULL, the safe option of MEM_COPY is assumed for all buffers.
int bwrite( req *c, const struct iovec *iov, int niov, const enum memflags_enum *flags )
{
    // total length of data to write
    int len = 0, rc = 0;
    for( unsigned int i = 0; i < niov; i++ )
        len += iov[i].iov_len;

    // If there's no buffer yet, try to write
    if( !c->wb )
    {
        rc = writev( c->fd, iov, niov );
        if( rc < 0 ) rc = 0;        // this could be due to blocking or unrecoverable system errors. Pretend nothing was written, write buffer code will handle it later.
        if( rc == len )
        {
            debug_printf( "===> Wrote %d bytes directy\n", rc );
            if( flags )
                for( unsigned int i = 0; i < niov; i++ )
                    if( flags[i] & MEM_FREE ) free( iov[i].iov_base );
            return SUCCESS;     // everything fine (system errors for a length zero write are ignored)
        }
    }

    // find end of write buffer list and calculate total current size
    struct wbchain_struct *last;
    int buflen = 0;
    for( last = c->wb; last && last->next; last = last->next )
        if( last->len > 0 )
            buflen += last->len;
    if( last ) buflen += last->len;
    if( buflen + len - rc > MAX_REP_LEN )
    {
        debug_printf( "===> Output buffer overflow\n" );
        return BUFFER_OVERFLOW;
    }

    // find partially written or unwritten buffers and add them to the write buffer chain
    for( unsigned int i = 0; i < niov; i++ )
    {
        if( rc >= iov[i].iov_len )
        {
            // the full buffer has been written
            rc -= iov[i].iov_len;
            continue;
        }
        // allocate new buffer
        struct wbchain_struct *wbc;
        const int l = iov[i].iov_len - rc;
        if( !flags || (flags[i] & MEM_COPY) )
        {
            // copy provided buffer
            wbc = malloc( sizeof(struct wbchain_struct) + l );
            if( !wbc )
            {
                perror( "malloc" );
                exit( EXIT_FAILURE );
            }
            wbc->f = MEM_COPY;
            wbc->payload.data = (char*)wbc + sizeof(struct wbchain_struct);
            memcpy( wbc->payload.data, iov[i].iov_base + rc, l );
            debug_printf( "===> buffered %d bytes (of %d) by copying\n", l, iov[i].iov_len );
        }
        else
        {
            // retain provided buffer
            wbc = malloc( sizeof(struct wbchain_struct) );
            if( !wbc )
            {
                perror( "malloc" );
                exit( EXIT_FAILURE );
            }
            wbc->f = flags[i] & (MEM_KEEP | MEM_FREE);      // sanitize user supplied flags
            wbc->payload.data = iov[i].iov_base + rc;
            debug_printf( "===> Buffered %d bytes (of %d)\n", l, iov[i].iov_len );
        }
        wbc->f |= MEM_PTR;
        wbc->len = l;
        wbc->offset = 0;
        wbc->next = NULL;
        // append buffer to write buffer list
        if( last )
            last->next = wbc;
        else
            c->wb = wbc;
        last = wbc;
        rc = 0;
    }

    return BUFFERED;
}

// try to send a file directly, and if that does not succeed append it to the internal write buffer
// flag indicates wether to close fd after it has been sent (FD_CLOSE) or to keep it open (FD_KEEP).
int bsendfile( req *c, int fd, off_t offset, int size, const enum memflags_enum flag )
{
    int rc = 0;

    // If there's no buffer yet, try to write
    if( !c->wb )
    {
        rc = sendfile( c->fd, fd, &offset, size );
        if( rc < 0 ) rc = 0;        // this could be due to blocking or unrecoverable system errors. Pretend nothing was written, write buffer code will handle it later.
        if( rc == size )
        {
            debug_printf( "===> Sent %d bytes from file without buffering\n", rc );
            if( flag & FD_CLOSE ) close( fd );
            return SUCCESS;     // everything fine (system errors for a length zero write are ignored)
        }
    }

    // find end of write buffer list
    struct wbchain_struct *last;
    for( last = c->wb; last && last->next; last = last->next );
    
    // allocate new buffer
    struct wbchain_struct *wbc = malloc( sizeof(struct wbchain_struct) );
    if( !wbc )
    {
        perror( "malloc" );
        exit( EXIT_FAILURE );
    }
    // fill buffer
    wbc->f = (flag & (FD_CLOSE | FD_KEEP)) | MEM_FD;    // sanitize user supplied flags
    wbc->payload.fd = fd;
    wbc->len = -(size-rc);
    wbc->offset = offset;
    wbc->next = NULL;
    // append buffer to write buffer list
    if( last )
        last->next = wbc;
    else
        c->wb = wbc;
    last = wbc;
    rc = 0;
    debug_printf( "===> Buffered %d bytes (of %d) from file\n", wbc->len, size );
    
    return BUFFERED;
}

// write a response with given body and headers (must include the initial HTTP response header)
// automatically adds Date header and respects HEAD requests
// if body is non-NULL, it is sent as a string with appropriate Content-Length header
// if body is NULL, and bodylen is non-null, the value is sent, expecting caller to send the data on its own
// flag is a memory flag for the body, see bwrite.
int write_response( req *c, const unsigned int code, const char* headers, const char* body, unsigned int bodylen, enum memflags_enum flag )
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
    enum memflags_enum flags[2];
    flags[0] = MEM_FREE;
    iov[0].iov_len = asprintf( (char** restrict) &(iov[0].iov_base),
                               "HTTP/1.%c %u %s\r\n" EXTRA_HEADER "%sContent-Length: %u\r\nDate: %s\r\n\r\n",
                               c->v == V_10 ? '0' : '1', code, get_response( code ), headers ? headers : "", bodylen, str );

    // first try to write everything right away using a single call. Most often this should succeed and write buffer is not used.
    flags[1] = flag;
    iov[1].iov_base = (char*)body;
    iov[1].iov_len = bodylen;
    return bwrite( c, iov, (body && (c->m != M_HEAD) && (bodylen > 0)) ? 2 : 1, flags );
}

// handle a query for a special dynamically generated file
int handle_dynamic_file( req *c )
{
    // find matching url
    unsigned int i;
    for( i = 0; handlers[i].url && strcmp( c->url, handlers[i].url ); i++ );

    if( handlers[i].handler )
        return handlers[i].handler( c );
    
    return FILE_NOT_FOUND;
}

// handle a file query for an embedded file
int handle_embedded_file( req *c )
{
    unsigned int i;
    for( i = 0; contents[i].url && strcmp( c->url, contents[i].url ); i++ );

    if( contents[i].url )
    {
#ifdef TIMESTAMP
        const char *inm = get_header_field( c, "If-None-Match:", 0 );
        if( inm && strcmp( inm, TIMESTAMP ) == 0 )
        {
            const int rc = write_response( c, HTTP_NOT_MODIFIED, contents[i].headers, NULL, 0, MEM_KEEP );
            debug_printf( "===> ETag %s matches on %s\n", TIMESTAMP, c->url );
            return rc == BUFFER_OVERFLOW ? CLOSE_SOCKET : SUCCESS;
        }
#endif
        const int rc = write_response( c, HTTP_OK, contents[i].headers, contents[i].body, contents[i].len, MEM_KEEP );
        debug_printf( "===> Send embedded file %s (ETag %s)\n", c->url, TIMESTAMP );
        return rc == BUFFER_OVERFLOW ? CLOSE_SOCKET : SUCCESS;
    }

    return FILE_NOT_FOUND;
}

// handle a disk file query
int handle_disk_file( req *c )
{
   if( strstr( c->url, ".." ) != NULL )
        return FILE_NOT_FOUND;

    const int len_WWW_DIR = strlen( WWW_DIR ),
              len_url = strlen( c->url ),
              len_DIR_INDEX = strlen( DIR_INDEX );
    if( len_WWW_DIR + len_url + len_DIR_INDEX >= PATH_MAX )
        return FILE_NOT_FOUND;

    char fn[PATH_MAX];
    memcpy( fn, WWW_DIR, len_WWW_DIR );
    memcpy( fn + len_WWW_DIR, c->url, len_url );
    fn[len_WWW_DIR + len_url] = '\0';
    if( (len_url == 0) || (c->url[len_url-1] == '/') )
    {
        memcpy( fn + len_WWW_DIR + len_url, DIR_INDEX, len_DIR_INDEX );
        fn[len_WWW_DIR + len_url + len_DIR_INDEX] = '\0';
    }
    debug_printf( "===> Trying to open file: %s\n", fn );

    // open file
    int fd = open( fn, O_RDONLY );
    if( fd < 0 )
        return FILE_NOT_FOUND;

    // file statistics
    struct stat sb;
    if( fstat( fd, &sb ) )
    {
        write_response( c, HTTP_SERVER_ERROR, NULL, "500 - Server error", 0, MEM_KEEP );
        close( fd );
        return CLOSE_SOCKET;
    }
    debug_printf( "===> File size, modification time (ETag): %ld, %ld\n", sb.st_size, sb.st_mtim.tv_sec );

    // write headers
    char *str;
    asprintf( &str, "ETag: \"%ld\"\r\nContent-Type: %s\r\n", sb.st_mtim.tv_sec, get_mime( fn ) );

    // check ETag
    const char *inm = get_header_field( c, "If-None-Match:", 0 );
    char *last = NULL;
    if( inm && (strtol( inm+1, &last, 10 ) == sb.st_mtim.tv_sec) && last && (*last == '"') )
    {
        const int rc = write_response( c, HTTP_NOT_MODIFIED, str, NULL, 0, MEM_KEEP );
        free( str );
        close( fd );
        debug_printf( "===> ETag %s matches on %s\n", inm, c->url );
        return rc == BUFFER_OVERFLOW ? CLOSE_SOCKET : SUCCESS;
    }

    // write response
    const int rc = write_response( c, HTTP_OK, str, NULL, sb.st_size, MEM_KEEP );
    free( str );
    if( rc == BUFFER_OVERFLOW )
    {
        close( fd );
        return CLOSE_SOCKET;
    }

    if( c->m != M_HEAD )
        bsendfile( c, fd, 0, sb.st_size, FD_CLOSE );
    else
        close( fd );

    debug_printf( "===> Sent disk file %s\n", fn );
    return SUCCESS;     // bsendfile always succeeds (can't buffer-overflow)
}

// finish request after it has been handled
int finish_request( req *c )
{
    // close connection if no keep-alive
    if( c->v == V_10 || c->f & FL_CLOSE )
        return CLOSE_SOCKET;

    // remove handled data from request buffer, ready for next request (allowing pipelining, keep-alive)
    int rem = c->len - c->rl;    // should never underflow but just to be sure
    debug_printf( "===> Request (%d bytes) finished: %d bytes left to parse\n", c->rl, rem );
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
    int rc;

    if( c->m != M_GET && c->m != M_POST && c->m != M_HEAD )
    {
        if( write_response( c, HTTP_NOT_ALLOWED, NULL, "405 - Not allowed", 0, MEM_KEEP ) == BUFFER_OVERFLOW )
            return CLOSE_SOCKET;
    }
    else
    {
        // try different mechanisms in order
        if( (rc = handle_dynamic_file( c ) ) == CLOSE_SOCKET )
            return CLOSE_SOCKET;
        else if( rc == FILE_NOT_FOUND )
        {
            if( (rc = handle_embedded_file( c )) == CLOSE_SOCKET )
                return CLOSE_SOCKET;
            else if( rc == FILE_NOT_FOUND )
            {
                if( (rc = handle_disk_file( c )) == CLOSE_SOCKET )
                    return CLOSE_SOCKET;
                else if( rc == FILE_NOT_FOUND )
                {
                    if( write_response( c, HTTP_NOT_FOUND, NULL, "404 - Not found", 0, MEM_KEEP ) == BUFFER_OVERFLOW )
                        return CLOSE_SOCKET;
                }
            }
        }
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
            return READ_DATA;           // need more data
    }
    else
        c->rl = tmp - c->data + 2 + 2*(c->f & FL_CRLF);

    debug_printf( "===> Trailers:\n" );

    // hooray! we have headers, parse them
    char *p = c->tail;
    while( p )
    {
        // find end of current trailer and zero terminate it => p points to current trailer line
        tmp = strstr( p, (c->f & FL_CRLF) ? "\r\n" : "\n" );    // always finds something as we checked for existing empty line above
        tmp[0] = tmp[c->f & FL_CRLF] = '\0';
        if( !*p ) break;    // found empty trailer => done reading trailers
        if( *p == ' ' || *p == '\t' )
        {
            write_response( c, HTTP_BAD_REQUEST, NULL, "400 - Bad request, obsolete trailer line folding", 0, MEM_KEEP );
            return CLOSE_SOCKET;
        }
        for( char *end = tmp-1; end >= p && (*end == ' ' || *end == '\t'); end-- )
            *end = '\0';       // trim white space at end and replace by NUL
        debug_printf( "     %s\n", p );
        // point p to next trailer
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
            if( !tmp ) return READ_DATA;
            // get next chunk length
            tmp += 1 + (c->f & FL_CRLF);
            char *perr;
            unsigned int chunklen = strtol( c->data + c->rl, &perr, 16 );
            if( *perr != '\n' && *perr != '\r' && *perr != ';' )
            {
                write_response( c, HTTP_BAD_REQUEST, NULL, "400 - Bad request", 0, MEM_KEEP );
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
                return READ_DATA;
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
        if( c->len < c->rl ) return READ_DATA;
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
            return READ_DATA;           // need more data
    }
    else
        c->body = tmp + 2*(1 + (c->f & FL_CRLF));      // this is where the body starts (2 or 4 forward)
    c->rl = c->body - c->data;  // request length so far (not including body)

    debug_printf( "===> Headers:\n" );

    // hooray! we have headers, parse them
    char *p = c->head, *host = NULL;
    while( p )
    {
        // find end of current header and zero terminate it => p points to current header line
        tmp = strstr( p, (c->f & FL_CRLF) ? "\r\n" : "\n" );    // always finds something as we checked for existing empty line above
        tmp[0] = tmp[c->f & FL_CRLF] = '\0';
        if( !*p ) break;    // found empty header => done reading headers
        if( *p == ' ' || *p == '\t' )
        {
            write_response( c, HTTP_BAD_REQUEST, NULL, "400 - Bad request, obsolete header line folding", 0, MEM_KEEP );
            return CLOSE_SOCKET;
        }
        // trim white space at end and replace by NUL
        for( char *end = tmp-1; end >= p && (*end == ' ' || *end == '\t'); end-- )
            *end = '\0';
        debug_printf( "     %s\n", p );
        // check for known headers we care about
        if( strncasecmp( p, "Content-Length:", 15 ) == 0 )
        {
            char *perr;
            unsigned int cl = strtol( p+15, &perr, 10 );
            if( *perr != '\0' || cl < 0 || (c->cl > 0 && cl != c->cl) )
            {
                write_response( c, HTTP_BAD_REQUEST, NULL, "400 - Bad request", 0, MEM_KEEP );
                return CLOSE_SOCKET;
            }
            if( cl > MAX_REQ_LEN )
            {
                write_response( c, HTTP_TOO_LARGE, NULL, "413 - Payload too large", 0, MEM_KEEP );
                return CLOSE_SOCKET;
            }
            c->cl = cl;
            c->rl += cl;
            debug_printf( "===> Content-Length: %d (%d total)\n", c->cl, c->rl );
        }
        else if( strncasecmp( p, "Transfer-Encoding:", 18 ) == 0 )
        {
            const char *val = p+18+strspn( p+18, " \t" );
            if( strcasecmp( val, "chunked" ) != 0 )
            {
                // c.f. http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html#sec10.5.2
                write_response( c, HTTP_NOT_IMPLEMENTED, NULL, "501 - requested Transfer-Encoding not implemented", 0, MEM_KEEP );
                return CLOSE_SOCKET;
            }
            c->f |= FL_CHUNKED;
        }
        else if( strncasecmp( p, "Host:", 5 ) == 0 )
        {
            if( host )
            {
                // c.f. https://tools.ietf.org/html/rfc7230
                write_response( c, HTTP_BAD_REQUEST, NULL, "400 - multiple Host headers", 0, MEM_KEEP );
                return CLOSE_SOCKET;
            }
            host = p+5+strspn( p+5, " \t" );
        }
        else if( strncasecmp( p, "Connection:", 11 ) == 0 )
        {
            const char *val = p+11+strspn( p+11, " \t" );
            if( strcasecmp( val, "close" ) == 0 )
                c->f |= FL_CLOSE;
        }
        // point p to next header
        p = tmp + 1 + (c->f & FL_CRLF);
    }

    // check if host header has been received
    if( c->v == V_11 && !host )
    {
        // c.f. https://tools.ietf.org/html/rfc7230
        write_response( c, HTTP_BAD_REQUEST, NULL, "400 - missing Host headers", 0, MEM_KEEP );
        return CLOSE_SOCKET;
    }

    c->s = STATE_BODY;
    return SUCCESS;
}

// parse the request line if it is available
int read_request( req *c )
{
    char* data = c->data;

    // Optionally ignore an empty line(s) at beginning of request (rfc7230, 3.5)
    data += strspn( data, "\r\n" );

    // Try to read the request line
    c->f |= FL_CRLF;
    char *tmp = strstr( data, "\r\n" );
    if( tmp == NULL )
    {
        tmp = strstr( data, "\n" );
        if( tmp == NULL ) return READ_DATA;     // we need more data
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
        c->query = tmp;     // point query string at last NUL of uri
        tmp++;
    }
    // version
    tmp += strspn( tmp, " \t" );
    c->version = tmp;

    // split uri into url and query sting
    tmp = strrchr( c->url, '?' );
    if( tmp )
    {
        *tmp = '\0';
        c->query = tmp;
    }
    
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

    debug_printf( "===> Version: %s\tMethod: %s\tURL: %s\tQuery: %s\n", c->version, c->method, c->url, c->query );

    // does it look like a valid request?
    if( c->v == V_UNKNOWN || c->m == M_UNKNOWN )
    {
        write_response( c, HTTP_BAD_REQUEST, NULL, "400 - Bad request", 0, MEM_KEEP );
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
            write_response( c, HTTP_TOO_LARGE, NULL, "413 - Payload too large", 0, MEM_KEEP );
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
    int rc = READ_DATA;
    const int nbytes = read( c->fd, c->data+c->len, len );
    if( nbytes == 0 )
        rc = CLOSE_SOCKET;  // nothing to read from this socket, must be closed => request finished
    else if( nbytes < 0 )
    {
        // these are retryable errors, all others are system errors where we just abandon the connection
        if( errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR )
            rc = CLOSE_SOCKET;
    }
    else
    {
        c->len += nbytes;
        c->data[c->len] = '\0';
        rc = parse_data( c );
    }

    // figure out what action to take next
    if( rc == CLOSE_SOCKET )
    {
        c->f |= FL_SHUTDOWN;
        return WRITE_DATA;      // flush write buffer then shutdown connection
    }
    else if( c->wb )
        return WRITE_DATA;      // flush write buffer, then continuing reading

    return READ_DATA;       // continue reading
}

// write internally buffered data to a socket.
int write_to_client( req *c )
{
    int rc;
    while( c->wb )
    {
        if( c->wb->f & MEM_PTR )
        {
            // try to write data
            rc = write( c->fd, c->wb->payload.data + c->wb->offset, c->wb->len );
            debug_printf( "===> Written %d buffered bytes of %d\n", rc, c->wb->len );
            if( rc < 0 )
            {
                // these are retryable errors, all others are system errors where we just abandon the connection
                if( (errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR) )
                    return CLOSE_SOCKET;
                else
                    return WRITE_DATA;
            }
            c->wb->offset += rc;
            c->wb->len -= rc;
            if( c->wb->len > 0 )
                return  WRITE_DATA;     // more data left to write, return till socket is ready for more
            if( c->wb->f & (MEM_FREE | MEM_COPY) ) free( c->wb->payload.data );
        }
        else if( c->wb->f & MEM_FD )
        {
            // try to send file
            rc = sendfile( c->fd, c->wb->payload.fd, &(c->wb->offset), -c->wb->len );
            debug_printf( "===> Sent %d buffered bytes of %d from file\n", rc, -c->wb->len );
            if( rc < 0 )
            {
                // these are retryable errors, all others are system errors where we just abandon the connection
                if( (errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR) )
                    return CLOSE_SOCKET;
                else
                    return WRITE_DATA;
            }
            c->wb->len += rc;           // len is negative for sendfile
            if( c->wb->len < 0 )
                return  WRITE_DATA;     // more data left to write, return till socket is ready for more
            if( c->wb->f & FD_CLOSE ) close( c->wb->payload.fd );
        }

        // finished this one. Free buffer and move on
        struct wbchain_struct *q = c->wb->next;
        free( c->wb );
        c->wb = q;
    }

    // everything written successfully
    return (c->f & FL_SHUTDOWN) ? CLOSE_SOCKET : READ_DATA;
}

// Main HTTP server program entry point (adapted from https://www.gnu.org/software/libc/manual/html_node/Waiting-for-I_002fO.html#Waiting-for-I_002fO)
int http_server_main( int argc, char *argv[] )
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
    short port = SERVER_PORT;
    if( argc > 1 )
        port = (short)strtol( argv[1], NULL, 10 );
    struct sockaddr_in serverAddr = { 0 };
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons( port );
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
    for( unsigned int i = 0; i < MAX_CONNECTIONS; i++ )
        fds[i].fd = -1;
    fds[MAX_CONNECTIONS].fd = serverSocket;
    fds[MAX_CONNECTIONS].events = POLLIN;

    // main loop (exited only via signal handler)
    while( running )
    {
        // wait for input
        if( ppoll( fds, MAX_CONNECTIONS+1, NULL, &sset_enabled ) < 0 )
        {
            if( errno == EINTR ) continue;  // ignore interrupted system calls
            perror( "ppoll" );
            exit( EXIT_FAILURE );
        }

        // check server socket for new connections
        if( fds[MAX_CONNECTIONS].revents & (POLLRDHUP|POLLHUP|POLLERR|POLLNVAL) )
        {
            // something went wrong with the server socket, shut the whole thing down
            running = false;
            continue;
        }
        else if( fds[MAX_CONNECTIONS].revents & POLLIN )
        {
            const int new = accept( serverSocket, NULL, NULL );
            if( new < 0 )
            {
                perror( "accept" );
                exit( EXIT_FAILURE );
            }
            
            // find free connection slot
            unsigned int j;
            for( j = 0; j < MAX_CONNECTIONS && fds[j].fd >= 0; j++ );
            if( j == MAX_CONNECTIONS )
            {
                // can't handle any more clients. Client will have to retry later.
                write( new, "HTTP/1.1 503 Service unavailable\r\nContent-Length: 37\r\n\r\n503 - Service temporarily unavailable", 94 );
                shutdown( new, SHUT_RDWR );
                close( new );
                debug_printf( "===> Dropped connection\n" );
            }
            else
            {
                // try to switch socket to non-blocking I/O
                const int flags = fcntl( new, F_GETFL, 0 );
                if( flags != -1 )
                    fcntl( new, F_SETFL, flags | O_NONBLOCK );

                // initialize request and add to watchlist
                fds[j].fd = new;
                fds[j].events = POLLIN | POLLRDHUP;
                INIT_REQ( &reqs[j], new );
                debug_printf( "===> New connection\n" );
            }
        }

        // process all other client sockets
        for( unsigned int i = 0; i < MAX_CONNECTIONS; i++ )
        {
            if( fds[i].revents & (POLLRDHUP | POLLHUP | POLLERR | POLLNVAL) )
            {
                // other side hung up or somethign went wrong, completely close socket
                shutdown( fds[i].fd, SHUT_RDWR );
                close( fds[i].fd );
                fds[i].fd = -1;
                // free request data
                FREE_REQ( &reqs[i] );
                debug_printf( "===> Closed connection\n" );
            }
            else if( fds[i].revents & POLLOUT )
            {
                // ready to write out queued data
                const int res = write_to_client( &reqs[i] );
                if( res == CLOSE_SOCKET )
                {
                    // initiate shutdown sequence
                    shutdown( fds[i].fd, SHUT_WR );
                    fds[i].events = POLLRDHUP;                      // just wait for other side to hang up
                    debug_printf( "===> Closing connection\n" );
                }
                else if( res == READ_DATA )
                    fds[i].events = POLLRDHUP | POLLIN;             // all writing is done, switch to reading
            }
            else if( fds[i].revents & POLLIN )
            {
                // ready to (continue to) read next request
                const int res = read_from_client( &reqs[i] );
                if( res == WRITE_DATA )
                    fds[i].events = POLLRDHUP | POLLOUT;            // switch to writing
            }
        }
    }

    debug_printf( "===> Exiting\n" );
    // shut all connections down hard
    close( serverSocket );
    for( unsigned int j = 0; j < MAX_CONNECTIONS; j++ )
    {
        FREE_REQ( &reqs[j] );
        if( fds[j].fd < 0 ) continue;
        close( fds[j].fd );
    }

    return SUCCESS;
}

