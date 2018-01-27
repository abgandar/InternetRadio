// some common MIME types (note: extensions must be backwards for faster matching later!)
static const struct mimetype_struct mimetypes[] = {
#ifdef MORE_MIME_TYPES
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
#else
    // just the types needed for the on-disk version of the internet radio to work
    { "gnp.", "image/png" },
    { "lmth.", "text/html" },
    { "oci.", "image/x-icon" },
#endif
    { NULL }
};


// some simple default content
#ifdef DEFAULT_CONTENT

#ifdef TIMESTAMP
#define LM_HEADER "ETag: " TIMESTAMP "\r\n"
#else
#define LM_HEADER ""
#endif

static const struct content_struct contents[] = {
    { NULL, "/", CONT_EMBEDDED | CONT_PREFIX_MATCH, { .embedded = { "Content-Type: text/html\r\n" LM_HEADER,
        "<!doctype html><html><head><title>New website</title></html><body><h1>Welcome</h1><p>This is your new webserver which seems to be set up correctly.</p><body></html>", 156 } } },
    { NULL }
};
#endif
