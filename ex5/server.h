#include "globals.h"

/***
 * information used to start up a server
 */
struct server_setup_information{
    unsigned short port;
    std::string shm_pathname;
	int shm_proj_id;
    std::string info_file_name;
    std::string info_file_directory;
};

void start_communication(const server_setup_information& setup_info, live_server_info& server);
void create_info_file(const server_setup_information& setup_info, live_server_info& server);
int get_connection(const live_server_info& server);
void write_to_socket(const live_server_info& server, const std::string& msg);
void write_to_shm(const live_server_info& server, const std::string& msg);
void shutdown(const server_setup_information& setup_info, const live_server_info& server);
void run(const server_setup_information& setup_info, const std::string& shm_msg, const std::string& socket_msg);