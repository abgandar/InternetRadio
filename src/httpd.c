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
 * httpd.c
 *
 * Simple standalone HTTP server.
 *
 */

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "http-server.h"

// list of Base64 encoded user:pass strings
static const char* users[] = {
    "dGVzdDp0ZXN0",             // test:test
    "dXNlcjpwYXNzd29yZA==",     // user:password
    "dmljdG9yaWE6c2VjcmV0",     // victoria:secret
    NULL
};

// handle basic authentication
static int handle_basic_auth( req *c, const struct content_struct *cs )
{
    const char* auth = get_header( c, "Authorization:" );
    bool allow = false;

    if( auth && !strncmp( auth, "Basic ", 6 ) )
    {
        auth += 6;
        for( unsigned int i = 0; !allow && users[i]; i++ )
            allow = (strcmp( users[i], auth ) == 0);
    }

    if( !allow )
        return write_response( c, HTTP_UNAUTHORIZED, "WWW-Authenticate: Basic realm=\"Test server\"", "401 - Unauthorized", 0, MEM_KEEP ) == BUFFER_OVERFLOW ? CLOSE_SOCKET : SUCCESS;
    else
        return FILE_NOT_FOUND;  // this makes the server fall through to the next content entry, effectively allowing access
}

// list of content
static const struct content_struct contents[] = {
    CONTENT_DYNAMIC( NULL, "/", CONT_PREFIX_MATCH, &handle_basic_auth, NULL ),
    CONTENT_DISK( NULL, "/", CONT_PREFIX_MATCH, "/", "index.html", DISK_LIST_DIRS ),
    CONTENT_END
};

// Main HTTP server program entry point
int main( int argc, char *argv[] )
{
    struct server_config_struct config;

    // set up server configuration
    http_server_config_defaults( &config );
    config.unpriv_user = "www-data";
    config.chroot = "/var/www/html/";
    config.contents = contents;
    http_server_config_argv( &argc, &argv, &config );

    // run server
    return http_server_main( &config );
}
