#ifndef GAME_H
#define GAME_H

#include "SFML/Graphics.hpp"
#include <memory>

class Game {
        static Game m_instance;
    public:
        static Game& i() { return m_instance; }

        void init(float _dt);
        void run();
        void restart();
        void update();
        void render();

        float getDeltaTime();
        void setDeltaTime(float _dt);
        float getTime();
        sf::Vector2f getViewSize();

    private:
        float m_accumulator = 0.0f;
        float m_dt = 1 / 125.0f;
        sf::RenderWindow m_window;
        sf::Vector2f m_view_size;
        float m_global_timer = 0.0f;
        bool m_first_launch = true;
        sf::Font m_font;
};

#endif // GAME_H
