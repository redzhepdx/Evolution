#include "Node.h"
#include "utility.h"

Node::Node(){
}

Node::~Node() {}


void Node::init(b2World* world, const sf::Vector2f& pos, float friction) {
    body_def.type = b2_dynamicBody; //this will be a dynamic body
    body_def.position.Set(-10, 20); //a little to the left
    body_def.fixedRotation = true;
    body = world->CreateBody(&body_def);

    shape.m_p.Set(0, 0); //position, relative to body position
    shape.m_radius = 0.5; //radius

    fixture_def.shape = &shape; //this is a pointer to the shape above
    fixture_def.friction = friction;

    b2Filter filter;
    filter.groupIndex = -2;
    fixture_def.filter = filter;
    body->CreateFixture(&fixture_def); //add a fixture to the body
    setPosition(pos);
    c = sf::Color(random_float(0, 255), random_float(0, 255), random_float(0, 255));
}

void Node::setPosition(const sf::Vector2f& pos) {
    body->SetTransform(b2Vec2(pos.x, pos.y), body->GetAngle());
}

void Node::render(sf::RenderTarget& rt) {
    sf::CircleShape circ;
    circ.setFillColor(c);
    circ.setRadius(shape.m_radius);
    circ.setOrigin(circ.getRadius(), circ.getRadius());
    circ.setPosition(body->GetPosition().x, body->GetPosition().y);
    rt.draw(circ);
}
