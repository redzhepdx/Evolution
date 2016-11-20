#include "Game.h"
#include "utility.h"
#include <iostream>
Game Game::m_instance;

void Game::init(float _dt) {
    setDeltaTime(_dt);
    m_view_size = sf::Vector2f(1280, 720);
    m_font.loadFromFile("resources/visitor2.ttf");
}

void Game::restart() {
    m_first_launch = false;
}

void Game::run() {
    sf::ContextSettings window_settings;
    window_settings.antialiasingLevel = 4;
    m_window.create(sf::VideoMode(m_view_size.x, m_view_size.y), "Aimbot | 13011057 | Tolga Ay", sf::Style::Default, window_settings);
    sf::Clock clock;

    while(m_window.isOpen()){
        // Check events
        sf::Event event;
        while(m_window.pollEvent(event)){
            if(event.type == sf::Event::Closed) m_window.close();
        }

        // Update
        m_accumulator += clock.restart().asSeconds();
        while(m_accumulator > m_dt){
            m_accumulator -= m_dt;

            update();
        }

        // Render
        render();
    }
}

void Game::update() {
    m_global_timer += m_dt;
}

void Game::render() {
    m_window.clear(sf::Color(0, 0, 0));
    m_window.display();
}


float Game::getDeltaTime() { return m_dt; }
void Game::setDeltaTime(float _dt) { m_dt = _dt; }
float Game::getTime() { return m_global_timer; }
sf::Vector2f Game::getViewSize() { return m_view_size; }
