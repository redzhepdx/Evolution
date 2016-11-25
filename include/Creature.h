#ifndef CREATURE_H
#define CREATURE_H

#include "Node.h"

class Muscle {
    public:
        Muscle(b2World* world, b2DistanceJointDef& def, float short_len_, float long_len_,
               float extend_time_, float contract_time_, float strength_);

        void update(float c_time, float dt);
        void render(sf::RenderTarget& rt);

        b2DistanceJoint* joint = nullptr;
        float contract_time = 0.9f;
        float extend_time = 0.1f;

        float long_len = 3.0f;
        float short_len = 1.5f;
        float* target_len = nullptr;
        float strength = 1.0f;
};

class Creature {
    public:
        Creature();

        void init(b2World* world, const sf::Vector2f& pos);
        void update(float dt);
        void render(sf::RenderTarget& rt);
        std::vector<Node> nodes;
        std::vector<Muscle> muscles;

        float timer = 0.0f;
        float heart_beat = 0.5f;
};

#endif // CREATURE_H
