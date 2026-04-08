// SPDX-License-Identifier: GPL-3.0-only

#include <cerrno>
#include <cstring>
#include <iostream>
#include <new>

#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ipc_udp.h"

UdpChannel::UdpChannel(int port_local, int port_remote, void *user_data)
    : port_local_(port_local),
      port_remote_(port_remote),
      user_data_(user_data) {
    std::memset(&remote_addr_, 0, sizeof(remote_addr_));
}

UdpChannel::~UdpChannel() {
    stop();
}

bool UdpChannel::init() {
    socket_send_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_send_ < 0) {
        perror("Failed to create UDP send socket");
        return false;
    }

    remote_addr_.sin_family = AF_INET;
    remote_addr_.sin_port = htons(port_remote_);
    if (::inet_pton(AF_INET, "127.0.0.1", &remote_addr_.sin_addr) <= 0) {
        perror("Invalid remote address");
        return false;
    }

    socket_recv_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_recv_ < 0) {
        perror("Failed to create UDP recv socket");
        return false;
    }

    sockaddr_in local_addr;
    std::memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port_local_);
    if (::inet_pton(AF_INET, "127.0.0.1", &local_addr.sin_addr) <= 0) {
        perror("Invalid local address");
        return false;
    }

    if (::bind(socket_recv_, reinterpret_cast<sockaddr *>(&local_addr), sizeof(local_addr)) < 0) {
        perror("Failed to bind UDP recv socket");
        return false;
    }

    if (cb_) {
        running_.store(true);
        recv_thread_ = std::thread(&UdpChannel::recv_loop, this);
    }

    return true;
}

void UdpChannel::stop() {
    running_.store(false);

    if (socket_recv_ >= 0) {
        ::shutdown(socket_recv_, SHUT_RDWR);
    }

    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }

    if (socket_send_ >= 0) {
        ::close(socket_send_);
        socket_send_ = -1;
    }

    if (socket_recv_ >= 0) {
        ::close(socket_recv_);
        socket_recv_ = -1;
    }
}

int UdpChannel::send(const char *data, int len) {
    if (socket_send_ < 0 || !data || len <= 0) {
        return -1;
    }

    ssize_t bytes_sent = ::sendto(socket_send_,
                                  data,
                                  len,
                                  0,
                                  reinterpret_cast<sockaddr *>(&remote_addr_),
                                  sizeof(remote_addr_));
    if (bytes_sent != len) {
        perror("Failed to send UDP data");
        return -1;
    }

    return 0;
}

int UdpChannel::recv(unsigned char *data, int maxlen, int *retlen) {
    if (socket_recv_ < 0 || !data || maxlen <= 0 || !retlen) {
        return -1;
    }

    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    ssize_t bytes_received = ::recvfrom(socket_recv_,
                                        data,
                                        maxlen,
                                        0,
                                        reinterpret_cast<sockaddr *>(&client_addr),
                                        &client_len);
    if (bytes_received < 0) {
        perror("Failed to receive UDP data");
        return -1;
    }

    *retlen = static_cast<int>(bytes_received);
    return 0;
}

void UdpChannel::set_callback(int (*cb)(char *buffer, size_t size, void *user_data)) {
    cb_ = cb;
}

void UdpChannel::recv_loop() {
    std::cout << "Listening on port_local " << port_local_ << std::endl;

    char buffer[2048];
    while (running_.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(socket_recv_, &rfds);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000;

        int ready = ::select(socket_recv_ + 1, &rfds, nullptr, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (running_.load()) {
                perror("UDP select failed");
            }
            break;
        }

        if (ready == 0 || !FD_ISSET(socket_recv_, &rfds)) {
            continue;
        }

        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        ssize_t bytes_received = ::recvfrom(socket_recv_,
                                            buffer,
                                            sizeof(buffer),
                                            0,
                                            reinterpret_cast<sockaddr *>(&client_addr),
                                            &client_len);
        if (bytes_received <= 0) {
            if (bytes_received < 0 && errno != EINTR && running_.load()) {
                perror("UDP recvfrom failed");
            }
            continue;
        }

        if (cb_) {
            cb_(buffer, static_cast<size_t>(bytes_received), user_data_);
        }
    }
}

namespace {

int udp_send_data(ipc_endpoint_t *pendpoint, const char *data, int len) {
    if (!pendpoint || !pendpoint->priv) {
        return -1;
    }
    auto *impl = static_cast<UdpChannel *>(pendpoint->priv);
    return impl->send(data, len);
}

int udp_recv_data(ipc_endpoint_t *pendpoint, unsigned char *data, int maxlen, int *retlen) {
    if (!pendpoint || !pendpoint->priv) {
        return -1;
    }
    auto *impl = static_cast<UdpChannel *>(pendpoint->priv);
    return impl->recv(data, maxlen, retlen);
}

}  // namespace

p_ipc_endpoint_t ipc_endpoint_create_udp(int port_local, int port_remote, transfer_callback_t cb, void *user_data) {
    auto *pendpoint = new (std::nothrow) ipc_endpoint_t();
    if (!pendpoint) {
        return nullptr;
    }

    auto *impl = new (std::nothrow) UdpChannel(port_local, port_remote, user_data);
    if (!impl) {
        delete pendpoint;
        return nullptr;
    }

    impl->set_callback(cb);
    if (!impl->init()) {
        delete impl;
        delete pendpoint;
        return nullptr;
    }

    pendpoint->priv = impl;
    pendpoint->cb = cb;
    pendpoint->user_data = user_data;
    pendpoint->send = udp_send_data;
    pendpoint->recv = udp_recv_data;
    return pendpoint;
}

void ipc_endpoint_destroy_udp(p_ipc_endpoint_t pendpoint) {
    if (!pendpoint) {
        return;
    }

    auto *impl = static_cast<UdpChannel *>(pendpoint->priv);
    delete impl;
    delete pendpoint;
}
