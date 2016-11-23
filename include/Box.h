#ifndef BOX_H
#define BOX_H

#include <Box2D/Box2D.h>
#include <SFML/Graphics.hpp>
class Box
{
    public:
        Box();
        virtual ~Box();
        void init(b2World* world, const sf::Vector2f& position, const sf::Vector2f& dimensions, const sf::Color& color = sf::Color::White);

        b2Body* getBody() const { return m_body; }
        b2Fixture* getFixture() const { return m_fixture; }
        const sf::Vector2f& getDimensions() const { return m_dimensions; };
        sf::Color c;
    private:
        b2Body* m_body = nullptr;
        b2Fixture* m_fixture = nullptr;
        sf::Vector2f m_dimensions;
};

#endif // BOX_H
