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
#include <stdlib.h>
#include <string.h>
#if DEBUG>1
#include <malloc.h>
#endif
#include <sys/types.h>

#include "config.h"
#include "ir-common.h"
#include "ir-server.h"

// various external binary files linked in later
extern const char _binary_ir_html_start;
extern const char _binary_ir_html_size;
extern const char _binary_radio_ico_start;
extern const char _binary_radio_ico_size;
extern const char _binary_radio_0_75x_png_start;
extern const char _binary_radio_0_75x_png_size;
extern const char _binary_radio_1x_png_start;
extern const char _binary_radio_1x_png_size;
extern const char _binary_radio_2_6x_png_start;
extern const char _binary_radio_2_6x_png_size;
extern const char _binary_radio_2x_png_start;
extern const char _binary_radio_2x_png_size;
extern const char _binary_radio_4x_png_start;
extern const char _binary_radio_4x_png_size;
extern const char _binary_radio_5_3x_png_start;
extern const char _binary_radio_5_3x_png_size;
#ifdef EASTEREGG
extern const char _binary_easteregg_png_start;
extern const char _binary_easteregg_png_size;
#endif

// handle CGI queries
int handle_ir_cgi( req *c, const struct content_struct *cs )
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
    if( c->method == M_POST )
        query = c->body;    // rely on body being null terminated
    else
        query = c->query;
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
    
    write_response( c, rc ? HTTP_SERVER_ERROR : HTTP_OK, head, body, obuf_size, MEM_COPY ); // unfortunately can't auto-free body as it may point inside obuf
    debug_printf( "===> CGI response:\n%s\n", body );
    
    // clean up
    free( obuf );
    return SUCCESS;
}

#ifdef TIMESTAMP
#define LM_HEADER "ETag: " TIMESTAMP "\r\n"
#else
#define LM_HEADER ""
#endif

// list of embedded files to serve directly
static const struct content_struct contents[] = {
    { NULL, "/cgi-bin/ir.cgi",    CONT_DYNAMIC,       { .dynamic = { &handle_ir_cgi, NULL } } },
    { NULL, "/",                  CONT_EMBEDDED,      {
        .embedded = {       "Content-Type: text/html\r\n" LM_HEADER,    &_binary_ir_html_start,         (unsigned int)&_binary_ir_html_size } }
    },
    { NULL, "/ir.html",           CONT_EMBEDDED,      {
        .embedded = {       "Content-Type: text/html\r\n" LM_HEADER,    &_binary_ir_html_start,         (unsigned int)&_binary_ir_html_size } }
    },
    { NULL, "/radio.ico",         CONT_EMBEDDED,      {
        .embedded = {       "Content-Type: image/png\r\n" LM_HEADER,    &_binary_radio_ico_start,       (unsigned int)&_binary_radio_ico_size } }
    },
    { NULL, "/radio-0-75x.png",   CONT_EMBEDDED,      {
        .embedded = {       "Content-Type: image/png\r\n" LM_HEADER,    &_binary_radio_0_75x_png_start, (unsigned int)&_binary_radio_0_75x_png_size } }
    },
    { NULL, "/radio-1x.png",      CONT_EMBEDDED,      {
        .embedded = {       "Content-Type: image/png\r\n" LM_HEADER,    &_binary_radio_1x_png_start,    (unsigned int)&_binary_radio_1x_png_size } }
    },
    { NULL, "/radio-2x.png",      CONT_EMBEDDED,      {
        .embedded = {       "Content-Type: image/png\r\n" LM_HEADER,    &_binary_radio_2x_png_start,    (unsigned int)&_binary_radio_2x_png_size } }
    },
    { NULL, "/radio-2-6x.png",    CONT_EMBEDDED,      {
        .embedded = {       "Content-Type: image/png\r\n" LM_HEADER,    &_binary_radio_2_6x_png_start,  (unsigned int)&_binary_radio_2_6x_png_size } }
    },
    { NULL, "/radio-4x.png",      CONT_EMBEDDED,      {
        .embedded = {       "Content-Type: image/png\r\n" LM_HEADER,    &_binary_radio_4x_png_start,    (unsigned int)&_binary_radio_4x_png_size } }
    },
    { NULL, "/radio-5-3x.png",    CONT_EMBEDDED,      {
        .embedded = {       "Content-Type: image/png\r\n" LM_HEADER,    &_binary_radio_5_3x_png_start,  (unsigned int)&_binary_radio_5_3x_png_size } }
    },
#ifdef EASTEREGG
    { NULL, "/hidden/easteregg",  CONT_DYNAMIC,      {
        .embedded = {       "Content-Type: image/png\r\n" LM_HEADER,    &_binary_easteregg_png_start,   (unsigned int)&_binary_easteregg_png_size } }
    },
#endif
    { NULL, "/hidden/redirect",   CONT_DYNAMIC | CONT_DIR_MATCH,      { .dynamic = { &handle_redirect, "http://www.web.de/" } } },
    { "192.168.1.6", "/",         CONT_DISK | CONT_PREFIX_MATCH,      {
        .disk = { "/var/www/html/", "ir.html", DISK_LIST_DIRS } }
    },
    { NULL, NULL }
};

// Main HTTP server program entry point
int main( int argc, char *argv[] )
{
    struct server_config_struct config;

    // set up server configuration
    http_server_config_defaults( &config );
    config.unpriv_user = "mpd";
    config.contents = contents;
    http_server_config_argv( &argc, &argv, &config );

    // run server
    const int rc = http_server_main( &config );

    // clean up
    disconnectMPD( );

#if DEBUG>1
    malloc_info( 0, stderr );
#endif

    return rc;
}
