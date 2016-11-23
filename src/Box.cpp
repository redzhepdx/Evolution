#include "Box.h"

Box::Box()
{
    //ctor
}

Box::~Box()
{
    //dtor
}

void Box::init(b2World* world, const sf::Vector2f& position, const sf::Vector2f& dimensions, const sf::Color& color) {
    c = color;
    m_dimensions = dimensions;
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;

    bodyDef.position.Set(position.x, position.y);

    m_body = world->CreateBody(&bodyDef);

    b2PolygonShape boxShape;
    boxShape.SetAsBox(dimensions.x*0.5f, dimensions.y*0.5f);

    b2FixtureDef fixtureDef;
    fixtureDef.shape = &boxShape;
    fixtureDef.density = 1.0f;
    fixtureDef.friction = 0.3f;
    m_fixture = m_body->CreateFixture(&fixtureDef);
}
