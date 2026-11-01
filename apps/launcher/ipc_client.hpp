/* Giochi Konsol OS v1.2 — cliente dos sockets de controle. */
#ifndef GIOCHI_IPC_CLIENT_H
#define GIOCHI_IPC_CLIENT_H

#include <stddef.h>

/* Envia um comando de uma linha e copia a resposta em reply. 0/-1. */
int giochi_ipc_request(const char *sock_path, const char *command,
                       char *reply, size_t replysz, int timeout_ms);

#endif /* GIOCHI_IPC_CLIENT_H */
