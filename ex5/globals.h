#include <string>

#define SHARED_MEMORY_SIZE 4096
#define MAXHOSTNAME 512


/***
 * used to track an individual server over its lifetime from startup to shutdown
 */
struct live_server_info{
    int socket_fd;
	int shmid;
    std::string info_file_path;
};
