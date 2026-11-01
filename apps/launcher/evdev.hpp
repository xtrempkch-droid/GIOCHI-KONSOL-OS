/* Giochi Konsol OS v1.2 — leitura de /dev/input/event* (secao 5.1: DTB
 * isolado; o Launcher so conhece evdev).
 */
#ifndef GIOCHI_EVDEV_HPP
#define GIOCHI_EVDEV_HPP

#include <cstddef>

#include "input.hpp"

struct giochi_evdev;

giochi_evdev *giochi_evdev_open(char *err, std::size_t errsz);
void giochi_evdev_close(giochi_evdev *dev);

/* 1 = evento entregue, 0 = timeout, -1 = erro. Os codigos vem do mapa
 * carregado por input_load_button_map (secao 2.2). */
int giochi_evdev_poll(giochi_evdev *dev, int timeout_ms,
                      const giochi::InputState *map,
                      giochi::Button *button, bool *pressed);

#endif /* GIOCHI_EVDEV_HPP */
