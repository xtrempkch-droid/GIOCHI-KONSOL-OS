/* Arcade Volleyball - port jogavel do classico de 1988 para o Giochi Konsol OS.
 *
 * Arquivo unico, C++17 + SDL2, sem fonte e sem audio: tudo e desenhado com o
 * SDL_Renderer (retangulos, linhas e uma bola feita de spans horizontais).
 * Alvo: os portateis RK3326/RK3566 do projeto, entrada por gamepad evdev.
 *
 * A fisica roda numa resolucao logica fixa (1280x720) e o
 * SDL_RenderSetLogicalSize escala para o painel real - 640x480 na X35S/X35H,
 * 1280x720 na X55 - sem nenhuma constante do jogo depender do aparelho.
 *
 * Controles:
 *   teclado   A/D (ou setas) movem, W/espaco pulam, Esc ou Q saem
 *   gamepad   dpad esquerda/direita movem, A pula, Start sai
 *
 * O P2 e uma IA simples: o console nao tem um segundo controle.
 *
 * Build: make -C "Jogos/Arcade Volleyball"   (SDL2 via pkg-config)
 */

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <SDL.h>

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr int kGroundHeight = 50;
constexpr int kFps = 60;
constexpr float kGravity = 0.6f;

constexpr int kFloorY = kWindowHeight - kGroundHeight;

/* Paleta do tema dark_default (assets/theme.json): nenhuma cor inventada. */
constexpr SDL_Color kColorBackground = {15, 15, 15, 255};   /* #0F0F0F background */
constexpr SDL_Color kColorGround = {30, 30, 30, 255};       /* #1E1E1E surface */
constexpr SDL_Color kColorNet = {51, 51, 51, 255};          /* #333333 divider */
constexpr SDL_Color kColorPlayer1 = {229, 9, 20, 255};      /* #E50914 accent */
constexpr SDL_Color kColorPlayer2 = {160, 160, 160, 255};   /* #A0A0A0 text_secondary */
constexpr SDL_Color kColorBall = {229, 229, 229, 255};      /* #E5E5E5 text_primary */

struct Player {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    int width = 80;
    int height = 100;
    float speed = 8.0f;
    float jump_force = -14.0f;
    bool grounded = true;
    int score = 0;

    void place(float start_x)
    {
        x = start_x;
        y = static_cast<float>(kFloorY - height);
        vx = 0.0f;
        vy = 0.0f;
        grounded = true;
    }

    float center_x() const { return x + static_cast<float>(width) / 2.0f; }
};

struct Ball {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    int radius = 20;
    float max_speed = 15.0f;

    /* Saque do lado de quem fez o ponto, indo em direcao ao outro lado. */
    void serve(bool from_left)
    {
        x = from_left ? kWindowWidth * 0.25f : kWindowWidth * 0.75f;
        y = kWindowHeight * 0.3f;
        vx = from_left ? 4.0f : -4.0f;
        vy = 0.0f;
    }
};

struct Net {
    int x = 0;
    int y = 0;
    int width = 16;
    int height = 250;
};

class ArcadeVolleyball {
public:
    ArcadeVolleyball() = default;
    ArcadeVolleyball(const ArcadeVolleyball &) = delete;
    ArcadeVolleyball &operator=(const ArcadeVolleyball &) = delete;
    ~ArcadeVolleyball() { shutdown(); }

    bool init()
    {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::fprintf(stderr, "arcade-volleyball: SDL_Init: %s\n", SDL_GetError());
            return false;
        }
        sdl_ready_ = true;

        window_ = SDL_CreateWindow("Arcade Volleyball - Giochi OS",
                                   SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   kWindowWidth, kWindowHeight, SDL_WINDOW_SHOWN);
        if (window_ == nullptr) {
            std::fprintf(stderr, "arcade-volleyball: SDL_CreateWindow: %s\n",
                         SDL_GetError());
            return false;
        }

        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED |
                                                      SDL_RENDERER_PRESENTVSYNC);
        if (renderer_ == nullptr) {
            /* Sem aceleracao (framebuffer puro, driver dummy) ainda da para
             * jogar: cai para o renderer por software. */
            renderer_ = SDL_CreateRenderer(window_, -1, 0);
        }
        if (renderer_ == nullptr) {
            std::fprintf(stderr, "arcade-volleyball: SDL_CreateRenderer: %s\n",
                         SDL_GetError());
            return false;
        }
        (void)SDL_RenderSetLogicalSize(renderer_, kWindowWidth, kWindowHeight);

        p1_.place(kWindowWidth * 0.2f - 40.0f);
        p2_.place(kWindowWidth * 0.8f - 40.0f);
        net_.width = 16;
        net_.height = 250;
        net_.x = kWindowWidth / 2 - net_.width / 2;
        net_.y = kFloorY - net_.height;
        ball_.serve(true);
        return true;
    }

    void run()
    {
        constexpr Uint32 kFrameDelay = 1000 / kFps;

        while (running_) {
            const Uint32 frame_start = SDL_GetTicks();

            handle_events();
            update();
            render();

            const Uint32 elapsed = SDL_GetTicks() - frame_start;
            if (elapsed < kFrameDelay) {
                SDL_Delay(kFrameDelay - elapsed);
            }
        }
        shutdown();
    }

private:
    void shutdown()
    {
        if (controller_ != nullptr) {
            SDL_GameControllerClose(controller_);
            controller_ = nullptr;
        }
        if (renderer_ != nullptr) {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (sdl_ready_) {
            SDL_Quit();
            sdl_ready_ = false;
        }
    }

    /* --- entrada ------------------------------------------------------ */

    void open_controller(int device_index)
    {
        if (controller_ != nullptr) {
            return;
        }
        if (SDL_IsGameController(device_index) == SDL_FALSE) {
            return;
        }
        controller_ = SDL_GameControllerOpen(device_index);
        if (controller_ == nullptr) {
            std::fprintf(stderr, "arcade-volleyball: SDL_GameControllerOpen: %s\n",
                         SDL_GetError());
        }
    }

    void close_controller(Uint32 instance_id)
    {
        if (controller_ == nullptr) {
            return;
        }
        SDL_Joystick *joystick = SDL_GameControllerGetJoystick(controller_);
        if (joystick == nullptr ||
            static_cast<Uint32>(SDL_JoystickInstanceID(joystick)) != instance_id) {
            return;
        }
        SDL_GameControllerClose(controller_);
        controller_ = nullptr;
        p1_left_ = false;
        p1_right_ = false;
        p1_jump_ = false;
    }

    void handle_events()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_QUIT:
                running_ = false;
                break;
            case SDL_KEYDOWN:
                if (event.key.repeat != 0) {
                    break; /* auto-repeat do X nao deve re-disparar o pulo */
                }
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_q:
                    running_ = false;
                    break;
                case SDLK_a:
                case SDLK_LEFT:
                    p1_left_ = true;
                    break;
                case SDLK_d:
                case SDLK_RIGHT:
                    p1_right_ = true;
                    break;
                case SDLK_w:
                case SDLK_UP:
                case SDLK_SPACE:
                    p1_jump_ = true; /* borda: consumida em update() */
                    break;
                default:
                    break;
                }
                break;
            case SDL_KEYUP:
                switch (event.key.keysym.sym) {
                case SDLK_a:
                case SDLK_LEFT:
                    p1_left_ = false;
                    break;
                case SDLK_d:
                case SDLK_RIGHT:
                    p1_right_ = false;
                    break;
                default:
                    break;
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                open_controller(event.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                close_controller(event.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    p1_left_ = true;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    p1_right_ = true;
                    break;
                case SDL_CONTROLLER_BUTTON_A:
                    p1_jump_ = true;
                    break;
                case SDL_CONTROLLER_BUTTON_START:
                    running_ = false;
                    break;
                default:
                    break;
                }
                break;
            case SDL_CONTROLLERBUTTONUP:
                switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    p1_left_ = false;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    p1_right_ = false;
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }
    }

    /* --- simulacao ---------------------------------------------------- */

    void update()
    {
        update_player1();
        update_player2();
        update_ball();
        p1_jump_ = false; /* pulo e de borda: segurar o botao nao repica */
    }

    void update_player1()
    {
        if (p1_left_) {
            p1_.vx = -p1_.speed;
        } else if (p1_right_) {
            p1_.vx = p1_.speed;
        } else {
            p1_.vx = 0.0f;
        }

        if (p1_jump_ && p1_.grounded) {
            p1_.vy = p1_.jump_force;
            p1_.grounded = false;
        }

        p1_.vy += kGravity;
        p1_.x += p1_.vx;
        p1_.y += p1_.vy;

        if (p1_.x < 0.0f) {
            p1_.x = 0.0f;
        }
        const float left_limit = static_cast<float>(net_.x - p1_.width);
        if (p1_.x > left_limit) {
            p1_.x = left_limit;
        }

        const float floor_line = static_cast<float>(kFloorY - p1_.height);
        if (p1_.y >= floor_line) {
            p1_.y = floor_line;
            p1_.vy = 0.0f;
            p1_.grounded = true;
        }
    }

    void update_player2()
    {
        const float net_right = static_cast<float>(net_.x + net_.width);

        if (ball_.x > net_right) {
            const float delta = ball_.x - p2_.center_x();
            if (delta < -10.0f) {
                p2_.vx = -p2_.speed * 0.8f;
            } else if (delta > 10.0f) {
                p2_.vx = p2_.speed * 0.8f;
            } else {
                p2_.vx = 0.0f;
            }

            if (ball_.y < p2_.y && p2_.grounded &&
                std::fabs(ball_.x - p2_.center_x()) < 50.0f) {
                p2_.vy = p2_.jump_force;
                p2_.grounded = false;
            }
        } else {
            /* Volta para a posicao de espera enquanto a bola esta do outro lado. */
            const float home_x = kWindowWidth * 0.75f - static_cast<float>(p2_.width) / 2.0f;
            if (p2_.x < home_x - 10.0f) {
                p2_.vx = p2_.speed * 0.5f;
            } else if (p2_.x > home_x + 10.0f) {
                p2_.vx = -p2_.speed * 0.5f;
            } else {
                p2_.vx = 0.0f;
            }
        }

        p2_.vy += kGravity;
        p2_.x += p2_.vx;
        p2_.y += p2_.vy;

        if (p2_.x < net_right) {
            p2_.x = net_right;
        }
        if (p2_.x + p2_.width > kWindowWidth) {
            p2_.x = static_cast<float>(kWindowWidth - p2_.width);
        }

        const float floor_line = static_cast<float>(kFloorY - p2_.height);
        if (p2_.y >= floor_line) {
            p2_.y = floor_line;
            p2_.vy = 0.0f;
            p2_.grounded = true;
        }
    }

    void update_ball()
    {
        const float radius = static_cast<float>(ball_.radius);

        ball_.vy += kGravity * 0.7f;
        ball_.x += ball_.vx;
        ball_.y += ball_.vy;

        if (ball_.x - radius < 0.0f) {
            ball_.x = radius;
            ball_.vx = -ball_.vx;
            nudge_vertical();
        }
        if (ball_.x + radius > kWindowWidth) {
            ball_.x = static_cast<float>(kWindowWidth) - radius;
            ball_.vx = -ball_.vx;
            nudge_vertical();
        }
        if (ball_.y - radius < 0.0f) {
            ball_.y = radius;
            ball_.vy = -ball_.vy;
        }

        collide_player(p1_);
        collide_player(p2_);
        collide_net();

        /* Uma colisao pode empurrar a bola para fora da tela por um quadro
         * (jogador encostado na parede): recoloca dentro antes de desenhar. */
        ball_.x = std::clamp(ball_.x, radius,
                             static_cast<float>(kWindowWidth) - radius);
        ball_.y = std::max(ball_.y, radius);

        if (ball_.y + radius > static_cast<float>(kFloorY)) {
            if (ball_.x < static_cast<float>(net_.x)) {
                /* Caiu no lado do P1: ponto do P2. */
                ++p2_.score;
                ball_.serve(false);
            } else {
                ++p1_.score;
                ball_.serve(true);
            }
            std::fprintf(stdout, "arcade-volleyball: %d x %d\n",
                         p1_.score, p2_.score);
        }
    }

    /* A rede e uma parede: a bola so passa por cima do topo. Sem isso ela
     * atravessaria a rede e o ponto sairia do lado errado. */
    void collide_net()
    {
        const float radius = static_cast<float>(ball_.radius);
        const float net_left = static_cast<float>(net_.x);
        const float net_right = static_cast<float>(net_.x + net_.width);

        if (ball_.y + radius <= static_cast<float>(net_.y)) {
            return; /* passou por cima da rede */
        }
        if (ball_.x + radius <= net_left || ball_.x - radius >= net_right) {
            return; /* fora da faixa horizontal da rede */
        }

        if (ball_.vx > 0.0f) {
            ball_.x = net_left - radius;
            ball_.vx = -ball_.vx;
        } else if (ball_.vx < 0.0f) {
            ball_.x = net_right + radius;
            ball_.vx = -ball_.vx;
        } else {
            /* Bola parada em cima da fita: empurra para o lado mais proximo e
             * deixa a gravidade resolver (evita travar a partida). */
            ball_.x = (ball_.x < static_cast<float>(net_.x) + net_.width / 2.0f)
                          ? net_left - radius
                          : net_right + radius;
        }
    }

    void collide_player(const Player &player)
    {
        const float radius = static_cast<float>(ball_.radius);
        const float left = player.x;
        const float right = player.x + static_cast<float>(player.width);
        const float top = player.y;
        const float bottom = player.y + static_cast<float>(player.height);

        const float closest_x = std::clamp(ball_.x, left, right);
        const float closest_y = std::clamp(ball_.y, top, bottom);
        const float dx = ball_.x - closest_x;
        const float dy = ball_.y - closest_y;
        const float distance_squared = dx * dx + dy * dy;

        /* distance_squared == 0 significa centro da bola dentro do jogador:
         * sai pela borda no quadro seguinte, nao ha normal para refletir. */
        if (distance_squared <= 0.0f || distance_squared >= radius * radius) {
            return;
        }

        const float distance = std::sqrt(distance_squared);
        const float nx = dx / distance;
        const float ny = dy / distance;

        ball_.x = closest_x + nx * radius;
        ball_.y = closest_y + ny * radius;

        /* Quique quase vertical (bola batendo na cabeca do jogador, nx ~ 0)
         * cairia sempre na mesma vertical: o jogador se reposiciona e a bola
         * quica de novo na cabeca dele, sem nunca cruzar a rede (partida
         * travada). Nesse caso o desvio sai para o lado, afastando-se do
         * centro do jogador, com a componente horizontal cheia - o jogador
         * nao tem velocidade para acompanhar. */
        float kick_x = nx * ball_.max_speed;
        float kick_y = ny * ball_.max_speed;
        if (std::fabs(nx) < 0.2f) {
            const float side = (ball_.x < player.center_x()) ? -1.0f : 1.0f;
            kick_x = side * ball_.max_speed;
            if (kick_y < 0.0f) {
                kick_y *= 0.6f; /* sobe menos: a saida e lateral */
            }
        }

        ball_.vx = std::clamp(kick_x + player.vx * 0.5f,
                              -ball_.max_speed, ball_.max_speed);
        ball_.vy = std::clamp(kick_y + player.vy * 0.5f,
                              -ball_.max_speed, ball_.max_speed);

        /* A velocidade do jogador pode cancelar o empurrao lateral (bola
         * sobe reta e volta para a mesma cabeca). O toque lateral garante
         * que sempre exista componente horizontal. */
        nudge_vertical();
    }

    /* Quique praticamente vertical: a bola sobe e desce no mesmo x para
     * sempre. Devolve um empurrao lateral, como no port Godot
     * (scripts/sim/match.gd, _nudge_vertical). */
    void nudge_vertical()
    {
        if (std::fabs(ball_.vx) >= 0.5f) {
            return;
        }
        ball_.vx = (ball_.x <= kWindowWidth * 0.5f) ? 3.0f : -3.0f;
    }

    /* --- desenho ------------------------------------------------------ */

    void set_color(const SDL_Color &color)
    {
        (void)SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    }

    void fill_rect(int x, int y, int width, int height)
    {
        const SDL_Rect rect = {x, y, width, height};
        (void)SDL_RenderFillRect(renderer_, &rect);
    }

    void draw_ball()
    {
        const int center_x = static_cast<int>(std::lround(ball_.x));
        const int center_y = static_cast<int>(std::lround(ball_.y));
        const int radius = ball_.radius;

        set_color(kColorBall);
        for (int dy = -radius; dy <= radius; ++dy) {
            const int half = static_cast<int>(
                std::sqrt(static_cast<double>(radius * radius - dy * dy)));
            (void)SDL_RenderDrawLine(renderer_, center_x - half, center_y + dy,
                                     center_x + half, center_y + dy);
        }
    }

    /* Placar sem fonte: digitos de 7 segmentos em retangulos. */
    void draw_digit(int value, int x, int y)
    {
        static constexpr unsigned kSegments[10] = {
            0x3Fu, 0x06u, 0x5Bu, 0x4Fu, 0x66u,
            0x6Du, 0x7Du, 0x07u, 0x7Fu, 0x6Fu,
        };
        constexpr int kWidth = 28;
        constexpr int kHeight = 48;
        constexpr int kThickness = 6;
        constexpr int kSegmentHeight = (kHeight - 3 * kThickness) / 2;

        const unsigned mask = kSegments[value % 10];
        if ((mask & 0x01u) != 0u) { /* a - topo */
            fill_rect(x + kThickness, y, kWidth - 2 * kThickness, kThickness);
        }
        if ((mask & 0x02u) != 0u) { /* b - superior direita */
            fill_rect(x + kWidth - kThickness, y + kThickness, kThickness, kSegmentHeight);
        }
        if ((mask & 0x04u) != 0u) { /* c - inferior direita */
            fill_rect(x + kWidth - kThickness, y + 2 * kThickness + kSegmentHeight,
                      kThickness, kSegmentHeight);
        }
        if ((mask & 0x08u) != 0u) { /* d - base */
            fill_rect(x + kThickness, y + kHeight - kThickness,
                      kWidth - 2 * kThickness, kThickness);
        }
        if ((mask & 0x10u) != 0u) { /* e - inferior esquerda */
            fill_rect(x, y + 2 * kThickness + kSegmentHeight, kThickness, kSegmentHeight);
        }
        if ((mask & 0x20u) != 0u) { /* f - superior esquerda */
            fill_rect(x, y + kThickness, kThickness, kSegmentHeight);
        }
        if ((mask & 0x40u) != 0u) { /* g - meio */
            fill_rect(x + kThickness, y + (kHeight - kThickness) / 2,
                      kWidth - 2 * kThickness, kThickness);
        }
    }

    void draw_number(int value, int x, int y, bool right_aligned)
    {
        constexpr int kDigitWidth = 28;
        constexpr int kDigitGap = 8;

        value = std::clamp(value, 0, 99);
        const int digits = value < 10 ? 1 : 2;
        const int total_width = digits * kDigitWidth + (digits - 1) * kDigitGap;
        int cursor = right_aligned ? x - total_width : x;

        if (digits == 2) {
            draw_digit(value / 10, cursor, y);
            cursor += kDigitWidth + kDigitGap;
        }
        draw_digit(value % 10, cursor, y);
    }

    void draw_score()
    {
        set_color(kColorBall);
        draw_number(p1_.score, kWindowWidth / 2 - 80, 24, true);
        draw_number(p2_.score, kWindowWidth / 2 + 80, 24, false);
    }

    void render()
    {
        set_color(kColorBackground);
        (void)SDL_RenderClear(renderer_);

        set_color(kColorGround);
        fill_rect(0, kFloorY, kWindowWidth, kGroundHeight);

        set_color(kColorNet);
        fill_rect(net_.x, net_.y, net_.width, net_.height);

        set_color(kColorPlayer1);
        fill_rect(static_cast<int>(std::lround(p1_.x)),
                  static_cast<int>(std::lround(p1_.y)), p1_.width, p1_.height);

        set_color(kColorPlayer2);
        fill_rect(static_cast<int>(std::lround(p2_.x)),
                  static_cast<int>(std::lround(p2_.y)), p2_.width, p2_.height);

        draw_ball();
        draw_score();

        SDL_RenderPresent(renderer_);
    }

    bool running_ = true;
    bool sdl_ready_ = false;
    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_GameController *controller_ = nullptr;

    Player p1_;
    Player p2_;
    Ball ball_;
    Net net_;

    bool p1_left_ = false;
    bool p1_right_ = false;
    bool p1_jump_ = false;
};

} /* namespace */

int main()
{
    ArcadeVolleyball game;
    if (!game.init()) {
        return 1;
    }
    game.run();
    return 0;
}
