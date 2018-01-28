//#define DEBUG                   1                           // include debug output

#define SOCKET_PFAD             "/run/mpd/socket"           // MPD socket
#define REBOOT_WAIT             100000                      // time in micro-seconds to wait after sync()ing
//#define SYSTEMD                                             // Use systemd to reboot/shutdown
//#define AUTOPLAY                                            // Automatically start playing when loading playlists

//#define EASTEREGG                                           // include easteregg
//#define TINY                                                // don't include various extra data

#ifdef DEBUG
#define debug_printf(...) printf(__VA_ARGS__)
#else
#define debug_printf(...)
#endif
