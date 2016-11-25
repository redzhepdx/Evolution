#ifndef NODE_H
#define NODE_H

#include <Box2D/Box2D.h>
#include <SFML/Graphics.hpp>

class Node{
    public:
        Node();
        virtual ~Node();

        void init(b2World* world, const sf::Vector2f& pos);
        void setPosition(const sf::Vector2f& pos);
        void render(sf::RenderTarget& rt);
        sf::Color c;
        b2CircleShape shape;
        b2FixtureDef fixture_def;
        b2BodyDef body_def;
        b2Body* body = nullptr;
};

#endif // NODE_H
