#define SOCKET_PFAD             "/run/mpd/socket"           // MPD socket
#define OUTPUT_BUFFER_SIZE      (2*1024*1024)               // output buffer size (3 MB for now)
#define REBOOT_WAIT             100000                      // time in micro-seconds to wait after sync()ing
//#define SYSTEMD                                             // Use systemd to reboot/shutdown
//#define AUTOPLAY                                            // Automatically start playing when loading playlists
