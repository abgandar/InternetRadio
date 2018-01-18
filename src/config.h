//#define DEBUG                   1                           // include debug output

#define SOCKET_PFAD             "/run/mpd/socket"           // MPD socket
#define REBOOT_WAIT             100000                      // time in micro-seconds to wait after sync()ing
//#define SYSTEMD                                             // Use systemd to reboot/shutdown
//#define AUTOPLAY                                            // Automatically start playing when loading playlists

#define UNPRIV_USER             "mpd"                       // unpriviliged user to drop to after binding if started as root
#define SERVER_PORT             8080                        // port to bind to
#define SERVER_IP               "0.0.0.0"                   // IP address of interface to bind to
#define MAX_REQ_LEN             (1024*1024*1)               // Maximum allowed size of a request (1 MB)
#define WWW_DIR                 "/var/www/html"             // directory where to look for files (must end in /)
#define DIR_INDEX               "ir.html"                   // directory index file used when requesting a directory
#define EXTRA_HEADER            "Connection: Keep-Alive\r\nKeep-Alive: timeout=60,max=999999\r\nX-Frame-Options: SAMEORIGIN\r\n"  // extra headers to send with all replies
#define MAX_CONNECTIONS         32                          // max number of file descriptors (= concurrent connections), max FD_SETSIZE
//#define MORE_MIME_TYPES                                     // include more MIME types in database than minimum required for IR application
//#define EASTEREGG                                           // include easteregg

