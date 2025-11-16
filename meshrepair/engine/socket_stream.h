#pragma once

#include <iostream>
#include <streambuf>
#include <vector>

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#else
#    include <sys/socket.h>
#    include <netinet/in.h>
#    include <unistd.h>
#    include <arpa/inet.h>
typedef int socket_t;
#    define INVALID_SOCKET (-1)
#    define SOCKET_ERROR (-1)
#    define closesocket close
#endif

namespace MeshRepair {
namespace Engine {

    /**
     * Socket streambuf for std::iostream compatibility.
     * Allows using socket as std::istream/std::ostream.
     */
    class SocketStreambuf : public std::streambuf {
    public:
        explicit SocketStreambuf(socket_t socket);
        ~SocketStreambuf();

        // Prevent copying
        SocketStreambuf(const SocketStreambuf&) = delete;
        SocketStreambuf& operator=(const SocketStreambuf&) = delete;

    protected:
        // Input (reading from socket)
        int_type underflow() override;

        // Output (writing to socket)
        int_type overflow(int_type c = traits_type::eof()) override;
        int sync() override;

    private:
        socket_t socket_;
        std::vector<char> in_buffer_;
        std::vector<char> out_buffer_;
        static const size_t buffer_size = 8192;
    };

    /**
     * Socket input stream (for reading from socket).
     */
    class SocketIStream : public std::istream {
    public:
        explicit SocketIStream(socket_t socket);
        ~SocketIStream();

    private:
        SocketStreambuf streambuf_;
    };

    /**
     * Socket output stream (for writing to socket).
     */
    class SocketOStream : public std::ostream {
    public:
        explicit SocketOStream(socket_t socket);
        ~SocketOStream();

    private:
        SocketStreambuf streambuf_;
    };

    /**
     * Simple TCP socket server.
     * Listens for connections on a port and accepts one client.
     */
    class SocketServer {
    public:
        SocketServer();
        ~SocketServer();

        /**
         * Initialize socket library (Windows only).
         */
        static bool init_sockets();

        /**
         * Cleanup socket library (Windows only).
         */
        static void cleanup_sockets();

        /**
         * Start listening on a port.
         */
        bool listen(int port);

        /**
         * Accept one client connection (blocking).
         */
        socket_t accept_client();

        /**
         * Close server socket.
         */
        void close();

    private:
        socket_t server_socket_;
        int port_;
    };

}  // namespace Engine
}  // namespace MeshRepair
