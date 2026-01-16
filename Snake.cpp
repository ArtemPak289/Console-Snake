#include <chrono>
#include <deque>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

struct Vec2
{
    int x{};
    int y{};
    bool operator==(const Vec2 &other) const { return x == other.x && y == other.y; }
};

enum class Dir
{
    Up,
    Down,
    Left,
    Right
};

class Random
{
public:
    Random() : rng_(std::random_device{}()) {}
    int nextInt(int lo, int hi)
    {
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(rng_);
    }

private:
    std::mt19937 rng_;
};

#ifdef _WIN32
class Input
{
public:
    Input() { enableAnsi(); }
    char pollKey()
    {
        char ch = 0;
        if (_kbhit())
        {
            ch = static_cast<char>(_getch());
            if (ch == 0 || ch == static_cast<char>(224))
            {
                // swallow extended code
                if (_kbhit())
                {
                    (void)_getch();
                }
                ch = 0;
            }
        }
        return ch;
    }

private:
    void enableAnsi()
    {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE)
        {
            DWORD mode = 0;
            if (GetConsoleMode(hOut, &mode))
            {
                mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                (void)SetConsoleMode(hOut, mode);
            }
        }
    }
};
#else
class Input
{
public:
    Input() { enableRawMode(); }
    ~Input() { restoreMode(); }

    char pollKey()
    {
        char ch = 0;
        if (stdinReady())
        {
            ssize_t n = ::read(STDIN_FILENO, &ch, 1);
            if (n <= 0)
            {
                ch = 0;
            }
        }
        return ch;
    }

private:
    termios orig_{};
    bool hasOrig_{false};

    void enableRawMode()
    {
        termios t{};
        if (tcgetattr(STDIN_FILENO, &t) == 0)
        {
            orig_ = t;
            hasOrig_ = true;
            t.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
            t.c_cc[VMIN] = 0;
            t.c_cc[VTIME] = 0;
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &t);
        }
    }
    void restoreMode()
    {
        if (hasOrig_)
        {
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
        }
    }
    bool stdinReady()
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        int r = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &tv);
        return r > 0 && FD_ISSET(STDIN_FILENO, &set);
    }
};
#endif

class Renderer
{
public:
    Renderer(int w, int h) : w_(w), h_(h) {}
    void clearScreen() const
    {
        std::cout << "\x1b[2J\x1b[H";
    }
    void draw(const std::vector<std::string> &buf) const
    {
        clearScreen();
        for (const auto &line : buf)
        {
            std::cout << line << "\n";
        }
        std::cout.flush();
    }

private:
    int w_{};
    int h_{};
};

class Game
{
public:
    Game(int w, int h)
        : w_(w), h_(h), rnd_(), input_(), render_(w, h)
    {
        reset();
    }

    int run()
    {
        using clock = std::chrono::steady_clock;
        auto last = clock::now();

        while (!quit_)
        {
            handleInput();
            auto now = clock::now();
            auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - last);
            if (dt.count() >= tickMs_)
            {
                step();
                last = now;
            }
            drawFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        return 0;
    }

private:
    int w_{};
    int h_{};
    Random rnd_;
    Input input_;
    Renderer render_;

    std::deque<Vec2> snake_;
    Vec2 food_{};
    Dir dir_{Dir::Right};
    bool quit_{false};
    bool gameOver_{false};
    int score_{0};
    int tickMs_{110};

    void reset()
    {
        snake_.clear();
        snake_.push_back({w_ / 2, h_ / 2});
        snake_.push_back({w_ / 2 - 1, h_ / 2});
        snake_.push_back({w_ / 2 - 2, h_ / 2});
        dir_ = Dir::Right;
        quit_ = false;
        gameOver_ = false;
        score_ = 0;
        tickMs_ = 110;
        spawnFood();
    }

    void handleInput()
    {
        char c = input_.pollKey();
        if (c == 0)
        {
            return;
        }

        if (c == 'q' || c == 'Q')
        {
            quit_ = true;
            return;
        }
        if ((c == 'r' || c == 'R') && gameOver_)
        {
            reset();
            return;
        }

        Dir next = dir_;
        if (c == 'w' || c == 'W')
        {
            next = Dir::Up;
        }
        if (c == 's' || c == 'S')
        {
            next = Dir::Down;
        }
        if (c == 'a' || c == 'A')
        {
            next = Dir::Left;
        }
        if (c == 'd' || c == 'D')
        {
            next = Dir::Right;
        }

        if (!isOpposite(dir_, next))
        {
            dir_ = next;
        }
    }

    void step()
    {
        if (gameOver_)
        {
            return;
        }

        Vec2 head = snake_.front();
        Vec2 next = head;

        if (dir_ == Dir::Up)
        {
            next.y -= 1;
        }
        if (dir_ == Dir::Down)
        {
            next.y += 1;
        }
        if (dir_ == Dir::Left)
        {
            next.x -= 1;
        }
        if (dir_ == Dir::Right)
        {
            next.x += 1;
        }

        if (hitWall(next) || hitSelf(next))
        {
            gameOver_ = true;
            return;
        }

        snake_.push_front(next);

        if (next == food_)
        {
            score_ += 10;
            tickMs_ = std::max(55, tickMs_ - 2);
            spawnFood();
            return;
        }

        snake_.pop_back();
    }

    bool hitWall(const Vec2 &p) const
    {
        return p.x <= 0 || p.x >= w_ - 1 || p.y <= 0 || p.y >= h_ - 1;
    }

    bool hitSelf(const Vec2 &p) const
    {
        for (size_t i = 0; i < snake_.size(); i++)
        {
            if (snake_[i] == p)
            {
                return true;
            }
        }
        return false;
    }

    bool isOpposite(Dir a, Dir b) const
    {
        if (a == Dir::Up && b == Dir::Down)
        {
            return true;
        }
        if (a == Dir::Down && b == Dir::Up)
        {
            return true;
        }
        if (a == Dir::Left && b == Dir::Right)
        {
            return true;
        }
        if (a == Dir::Right && b == Dir::Left)
        {
            return true;
        }
        return false;
    }

    void spawnFood()
    {
        Vec2 p{};
        bool ok = false;

        while (!ok)
        {
            p.x = rnd_.nextInt(1, w_ - 2);
            p.y = rnd_.nextInt(1, h_ - 2);
            ok = true;

            for (size_t i = 0; i < snake_.size(); i++)
            {
                if (snake_[i] == p)
                {
                    ok = false;
                }
            }
        }

        food_ = p;
    }

    std::vector<std::string> buildBuffer() const
    {
        std::vector<std::string> buf(h_, std::string(w_, ' '));

        for (int x = 0; x < w_; x++)
        {
            buf[0][x] = '#';
            buf[h_ - 1][x] = '#';
        }
        for (int y = 0; y < h_; y++)
        {
            buf[y][0] = '#';
            buf[y][w_ - 1] = '#';
        }

        buf[food_.y][food_.x] = '*';

        for (size_t i = 0; i < snake_.size(); i++)
        {
            const Vec2 &p = snake_[i];
            buf[p.y][p.x] = (i == 0) ? 'O' : 'o';
        }

        std::string hud = "Score: " + std::to_string(score_) + "   WASD=move  Q=quit";
        for (size_t i = 0; i < hud.size() && i + 2 < static_cast<size_t>(w_); i++)
        {
            buf[0][i + 2] = hud[i];
        }

        if (gameOver_)
        {
            std::string msg = "GAME OVER  (R=restart, Q=quit)";
            int start = std::max(1, (w_ - static_cast<int>(msg.size())) / 2);
            int y = h_ / 2;
            for (size_t i = 0; i < msg.size() && start + static_cast<int>(i) < w_ - 1; i++)
            {
                buf[y][start + static_cast<int>(i)] = msg[i];
            }
        }

        return buf;
    }

    void drawFrame() const
    {
        render_.draw(buildBuffer());
    }
};

int main()
{
    Game game(50, 22);
    return game.run();
}
