#ifndef IPC_UDP_H
#define IPC_UDP_H

#include <cstddef>

#ifdef __cplusplus
#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include <netinet/in.h>

class UdpChannel {
public:
    UdpChannel(int port_local, int port_remote, void *user_data = nullptr);
    ~UdpChannel();

    bool init();
    void stop();

    int send(const char *data, int len);
    int recv(unsigned char *data, int maxlen, int *retlen);

    void set_callback(int (*cb)(char *buffer, size_t size, void *user_data));

private:
    void recv_loop();

private:
    int port_local_ = -1;
    int port_remote_ = -1;
    int socket_send_ = -1;
    int socket_recv_ = -1;
    sockaddr_in remote_addr_{};

    int (*cb_)(char *buffer, size_t size, void *user_data) = nullptr;
    void *user_data_ = nullptr;

    std::atomic<bool> running_{false};
    std::thread recv_thread_;
};
#endif

/**
 * @brief 接收数据回调函数。
 */
typedef int (*transfer_callback_t)(char *buffer, size_t size, void *user_data);

/**
 * @brief 兼容层 endpoint 抽象（对外维持旧接口，内部由 UdpChannel 承载）。
 */
typedef struct ipc_endpoint_t {
    void *priv;         // UdpChannel*
    void *user_data;
    transfer_callback_t cb;

    int (*send)(struct ipc_endpoint_t *self, const char *data, int len);
    int (*recv)(struct ipc_endpoint_t *self, unsigned char *data, int maxlen, int *retlen);
} ipc_endpoint_t, *p_ipc_endpoint_t;

p_ipc_endpoint_t ipc_endpoint_create_udp(int port_local, int port_remote, transfer_callback_t cb, void *user_data);
void ipc_endpoint_destroy_udp(p_ipc_endpoint_t pendpoint);

#endif  // IPC_UDP_H
