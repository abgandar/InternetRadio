//#define DEBUG                   1                           // include debug output

#define SOCKET_PFAD             "/run/mpd/socket"           // MPD socket
#define REBOOT_WAIT             100000                      // time in micro-seconds to wait after sync()ing
//#define SYSTEMD                                             // Use systemd to reboot/shutdown
//#define AUTOPLAY                                            // Automatically start playing when loading playlists

//#define EASTEREGG                                           // include easteregg

#define UNPRIV_USER             "www"                       // unpriviliged user to drop to after binding if started as root
#define CHROOT_DIR              NULL                        // directory to chroot into when running the server
#define SERVER_PORT             80                          // port to bind to
#define SERVER_IP               "0.0.0.0"                   // IP address of interface to bind to
#define MAX_REQ_LEN             (1024*64)                   // Maximum allowed size of a request (64 kB)
#define MAX_HEAD_LEN            (1024*128)                  // Maximum allowed size of headers (128 kB)
#define MAX_BODY_LEN            (1024*1024*2)               // Maximum allowed size of body (2 MB)
#define MAX_WB_LEN              (1024*1024*10)              // Maximum allowed size of the write buffer (10 MB)
#define EXTRA_HEADERS           "Connection: Keep-Alive\r\nKeep-Alive: timeout=60,max=999999\r\nX-Frame-Options: SAMEORIGIN\r\n"  // extra headers to send with all replies
#define MAX_CONNECTIONS         32                          // max number of concurrent connections
#define MAX_CLIENT_CONN         4                           // max number of connections per client address
#define TIMEOUT                 60                          // approximate seconds to keep idle connections open
#define MORE_MIME_TYPES                                     // include more MIME types in database than minimum required for IR application
#define DEFAULT_CONTENT

#ifdef DEBUG
#define debug_printf(...) printf(__VA_ARGS__)
#else
#define debug_printf(...)
#endif
