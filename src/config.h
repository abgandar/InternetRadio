//#define DEBUG                   1                           // include debug output

#define SOCKET_PFAD             "/run/mpd/socket"           // MPD socket
#define REBOOT_WAIT             100000                      // time in micro-seconds to wait after sync()ing
//#define SYSTEMD                                             // Use systemd to reboot/shutdown
//#define AUTOPLAY                                            // Automatically start playing when loading playlists

#define UNPRIV_USER             "mpd"                       // unpriviliged user to drop to after binding if started as root
#define SERVER_PORT             80                          // port to bind to
#define SERVER_IP               "0.0.0.0"                   // IP address of interface to bind to
#define MAX_REQ_LEN             (1024*1024*1)               // Maximum allowed size of a request (1 MB)
#define MAX_REP_LEN             (1024*1024*10)              // Maximum allowed size of the write buffer (10 MB)
#define WWW_DIR                 "/var/www/html/"            // directory where to look for files (should end in / for safety)
#define DIR_INDEX               "ir.html"                   // directory index file used when requesting a directory from disk
#define EXTRA_HEADER            "Connection: Keep-Alive\r\nKeep-Alive: timeout=60,max=999999\r\nX-Frame-Options: SAMEORIGIN\r\n"  // extra headers to send with all replies
#define MAX_CONNECTIONS         32                          // max number of concurrent connections
//#define MORE_MIME_TYPES                                     // include more MIME types in database than minimum required for IR application
//#define EASTEREGG                                           // include easteregg


#ifdef DEBUG
#define debug_printf(...) printf(__VA_ARGS__)
#else
#define debug_printf(...)
#endif
