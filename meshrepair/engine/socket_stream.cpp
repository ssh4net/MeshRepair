#include "socket_stream.h"
#include <cstring>
#include <stdexcept>

namespace MeshRepair {
namespace Engine {

    // SocketStreambuf implementation

    SocketStreambuf::SocketStreambuf(socket_t socket)
        : socket_(socket)
        , in_buffer_(buffer_size)
        , out_buffer_(buffer_size)
    {
        // Set input buffer
        char* in_base = in_buffer_.data();
        setg(in_base, in_base, in_base);

        // Set output buffer
        char* out_base = out_buffer_.data();
        setp(out_base, out_base + buffer_size);
    }

    SocketStreambuf::~SocketStreambuf()
    {
        sync();
    }

    std::streambuf::int_type SocketStreambuf::underflow()
    {
        if (gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }

        // Read more data from socket
        int received = ::recv(socket_, in_buffer_.data(), static_cast<int>(buffer_size), 0);
        if (received <= 0) {
            return traits_type::eof();
        }

        // Update buffer pointers
        setg(in_buffer_.data(), in_buffer_.data(), in_buffer_.data() + received);

        return traits_type::to_int_type(*gptr());
    }

    std::streambuf::int_type SocketStreambuf::overflow(int_type c)
    {
        if (sync() == -1) {
            return traits_type::eof();
        }

        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(c);
            pbump(1);
        }

        return traits_type::not_eof(c);
    }

    int SocketStreambuf::sync()
    {
        // Flush output buffer to socket
        int num_bytes = static_cast<int>(pptr() - pbase());
        if (num_bytes > 0) {
            int sent = ::send(socket_, pbase(), num_bytes, 0);
            if (sent != num_bytes) {
                return -1;
            }
            pbump(-num_bytes);
        }
        return 0;
    }

    // SocketIStream implementation

    SocketIStream::SocketIStream(socket_t socket)
        : std::istream(&streambuf_)
        , streambuf_(socket)
    {
    }

    SocketIStream::~SocketIStream() {}

    // SocketOStream implementation

    SocketOStream::SocketOStream(socket_t socket)
        : std::ostream(&streambuf_)
        , streambuf_(socket)
    {
    }

    SocketOStream::~SocketOStream()
    {
        flush();
    }

    // SocketServer implementation

    SocketServer::SocketServer()
        : server_socket_(INVALID_SOCKET)
        , port_(0)
    {
    }

    SocketServer::~SocketServer()
    {
        close();
    }

    bool SocketServer::init_sockets()
    {
#ifdef _WIN32
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        return (result == 0);
#else
        return true;  // No initialization needed on Unix
#endif
    }

    void SocketServer::cleanup_sockets()
    {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool SocketServer::listen(int port)
    {
        port_ = port;

        // Create socket
        server_socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_socket_ == INVALID_SOCKET) {
            return false;
        }

        // Set socket options (reuse address)
        int reuse = 1;
        ::setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        // Bind to port
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces
        addr.sin_port = htons(static_cast<unsigned short>(port));

        if (::bind(server_socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(server_socket_);
            server_socket_ = INVALID_SOCKET;
            return false;
        }

        // Start listening
        if (::listen(server_socket_, 1) == SOCKET_ERROR) {
            closesocket(server_socket_);
            server_socket_ = INVALID_SOCKET;
            return false;
        }

        return true;
    }

    socket_t SocketServer::accept_client()
    {
        if (server_socket_ == INVALID_SOCKET) {
            return INVALID_SOCKET;
        }

        // Accept connection (blocking)
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        socket_t client_socket = ::accept(server_socket_, (sockaddr*)&client_addr, &client_len);

        return client_socket;
    }

    void SocketServer::close()
    {
        if (server_socket_ != INVALID_SOCKET) {
            closesocket(server_socket_);
            server_socket_ = INVALID_SOCKET;
        }
    }

}  // namespace Engine
}  // namespace MeshRepair
