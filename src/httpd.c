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

#include "http-server.h"

// list of embedded files to serve directly
static const struct content_struct contents[] = {
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
    config.chdir = "/var/www/html/";
    config.contents = contents;
    http_server_config_argv( &argc, &argv, &config );

    // run server
    return http_server_main( &config );
}
