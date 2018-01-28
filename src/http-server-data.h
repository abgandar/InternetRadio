// some common MIME types (note: extensions must be backwards for faster matching later!)
#ifndef TINY
static const struct mimetype_struct mimetypes[] = {
    // based on https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Complete_list_of_MIME_types
    { "caa.", "audio/aac"},
    { "iva.", "video/x-msvideo"},
    { "nib.", "application/octet-stream"},
    { "zb.", "application/x-bzip"},
    { "2zb.", "application/x-bzip2"},
    { "c.", "text/plain"},
    { "ppc.", "text/plain"},
    { "hsc.", "application/x-csh"},
    { "ssc.", "text/css"},
    { "vsc.", "text/csv"},
    { "xxc.", "text/plain"},
    { "tad.", "application/octet-stream" },
    { "cod.", "application/msword"},
    { "xcod.", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    { "toe.", "application/vnd.ms-fontobject"},
    { "bupe.", "application/epub+zip"},
    { "fig.", "image/gif"},
    { "lmth.", "text/html"},
    { "mth.", "text/html"},
    { "oci.", "image/x-icon"},
    { "sci.", "text/calendar"},
    { "raj.", "application/java-archive"},
    { "gpj.", "image/jpeg"},
    { "gepj.", "image/jpeg"},
    { "sj.", "application/javascript"},
    { "nosj.", "application/json"},
    { "idim.", "audio/midi"},
    { "dim.", "audio/midi"},
    { "gepm.", "video/mpeg"},
    { "gkpm.", "application/vnd.apple.installer+xml"},
    { "pdo.", "application/vnd.oasis.opendocument.presentation"},
    { "sdo.", "application/vnd.oasis.opendocument.spreadsheet"},
    { "tdo.", "application/vnd.oasis.opendocument.text"},
    { "fto.", "font/otf"},
    { "gnp.", "image/png"},
    { "h.", "text/plain"},
    { "pph.", "text/plain"},
    { "fdp.", "application/pdf"},
    { "tpp.", "application/vnd.ms-powerpoint"},
    { "xtpp.", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    { "rar.", "application/x-rar-compressed"},
    { "ftr.", "application/rtf"},
    { "hs.", "application/x-sh"},
    { "gvs.", "image/svg+xml"},
    { "fws.", "application/x-shockwave-flash"},
    { "rat.", "application/x-tar"},
    { "ffit.", "image/tiff"},
    { "fit.", "image/tiff"},
    { "ftt.", "font/ttf"},
    { "txt.", "text/plain"},
    { "dsv.", "application/vnd.visio"},
    { "vaw.", "audio/x-wav"},
    { "mbew.", "video/webm"},
    { "pbew.", "image/webp"},
    { "ffow.", "font/woff"},
    { "2ffow.", "font/woff2"},
    { "lmthx.", "application/xhtml+xml"},
    { "slx.", "application/vnd.ms-excel"},
    { "xslx.", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    { "lmx.", "application/xml"},
    { "piz.", "application/zip"},
    { "z7.", "application/x-7z-compressed"},
    { NULL }
};
#endif

// mapping between response code and human readable message
struct response_struct {
    const unsigned int code;
    const char *msg;
};

// some human readable equivalents to HTTP status codes above (must be sorted by code except for last!)
static const struct response_struct responses[] = {
    { HTTP_OK,                  "OK" },
    { HTTP_NOT_MODIFIED,        "Not modified" },
    { HTTP_REDIRECT,            "Permanent redirect" },
    { HTTP_BAD_REQUEST,         "Bad request" },
    { HTTP_FORBIDDEN,           "Forbidden" },
    { HTTP_NOT_FOUND,           "Not found" },
    { HTTP_NOT_ALLOWED,         "Method not allowed" },
    { HTTP_TOO_LARGE,           "Payload too large" },
    { HTTP_SERVER_ERROR,        "Server error" },
    { HTTP_NOT_IMPLEMENTED,     "Not implemented" },
    { HTTP_SERVICE_UNAVAILABLE, "Service unavailable" },
    { 0 }
};

// some simple default content
#ifndef TINY
    #ifdef TIMESTAMP
    #define LM_HEADER "ETag: " TIMESTAMP "\r\n"
    #else
    #define LM_HEADER ""
    #endif

static const struct content_struct contents[] = {
    CONTENT_EMBEDDED( NULL, "/", CONT_PREFIX_MATCH, "Content-Type: text/html\r\n" LM_HEADER,
        "<!doctype html><html><head><title>New website</title></html><body><h1>Welcome</h1><p>This is your new webserver which seems to be set up correctly.</p><body></html>", 164 ),
    CONTENT_END
};
#endif

// default server config
static const struct server_config_struct default_config = {
    "www-data",     // unpriv user
    NULL,           // chroot
    CONF_CLEAN_URL, // flags
    "",             // extra headers
    "0.0.0.0",      // server ip
    NULL,           // server ip6
    80,             // server port
    1024*64,        // max request line length
    1024*128,       // max header length
    1024*1024*2,    // max body length
    1024*1024*10,   // max writebuffer length
    32,             // max connections
    3,              // max connections per client
    60,             // timeout
#ifdef TINY
    NULL,           // content
    NULL            // mime
#else
    contents,       // content
    mimetypes       // mime
#endif
};

