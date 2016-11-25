#include "Creature.h"
#include "utility.h"

Muscle::Muscle(b2World* world, b2DistanceJointDef& def,
               float short_len_, float long_len_,
               float extend_time_, float contract_time_,
               float strength_) :
        short_len(short_len_), long_len(long_len_), extend_time(extend_time_),
        contract_time(contract_time_), strength(strength_){
    def.length = long_len;
    target_len = &short_len;
    joint = (b2DistanceJoint*) world->CreateJoint( &def );
    joint->SetLength(long_len);
}

Creature::Creature() {
}

void Creature::init(b2World* world, const sf::Vector2f& pos) {
    Node* prev = nullptr;
    for(int i = 0; i < 2; ++i) {
        nodes.emplace_back();

        sf::Vector2f offset(random_float(0, i), random_float(0, i));
        nodes.back().init(world, pos + offset, i*0.25);

        if(prev != nullptr) {
            b2DistanceJointDef jointDef;
            Node* a = prev;
            Node* b = &nodes.back();
            jointDef.bodyA = a->body;
            jointDef.bodyB = b->body;

            sf::Vector2f a2b = normalize(offset)*a->shape.m_radius;
            jointDef.localAnchorA = b2Vec2( a2b.x,  a2b.y);
            jointDef.localAnchorB = b2Vec2(-a2b.x, -a2b.y);
            jointDef.collideConnected = false;
            jointDef.frequencyHz = 0;
            muscles.push_back(Muscle(world, jointDef, 3.0, 6.0, 0.3, 0.7, 10));
        }

        prev = &nodes.back();
    }
}

void Creature::update(float dt) {
    if((timer += dt) > heart_beat) timer = 0;
    for(unsigned i = 0; i < muscles.size(); ++i) {
        muscles[i].update(timer/heart_beat, dt);
    }
}

void Muscle::update(float c_time, float dt) {
    target_len = (contract_time < extend_time && (c_time < contract_time || extend_time < c_time)) ||
                 (extend_time < contract_time && extend_time < c_time) ?
                &short_len : &long_len;

    joint->SetLength(joint->GetLength() + strength*((*target_len) - joint->GetLength())*dt);
}

void Muscle::render(sf::RenderTarget& rt) {
    sf::RectangleShape rect;

    sf::Vector2f a(joint->GetBodyA()->GetPosition().x, joint->GetBodyA()->GetPosition().y);
    sf::Vector2f b(joint->GetBodyB()->GetPosition().x, joint->GetBodyB()->GetPosition().y);
    rect.setSize(sf::Vector2f(0.5f - 0.2f*(joint->GetLength()-short_len)/(long_len-short_len), magnitude(a-b)));
    rect.setOrigin(rect.getSize().x*0.5f, rect.getSize().y);
    rect.setPosition(a);
    rect.rotate(vecToAngle(b-a));
    rect.setFillColor(sf::Color(30, 30, 30));

    rt.draw(rect);
}

void Creature::render(sf::RenderTarget& rt) {
    for(unsigned i = 0; i < muscles.size(); ++i) muscles[i].render(rt);

    for(unsigned i = 0; i < nodes.size(); ++i) nodes[i].render(rt);
}
