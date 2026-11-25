#ifndef GAME_H
#define GAME_H

#include <box2d/box2d.h>
#include "SFML/Graphics.hpp"
#include <memory>
#include <vector>
#include "Creature.h"

class Game {
	static Game m_instance;
public:
	static Game& i() { return m_instance; }

	void init(float _dt);
	void run();
	void restart();
	// Runs up to max_steps physics steps (stopping early at a generation boundary)
	// and returns how many were run.
	unsigned update(unsigned max_steps = 1);
	void render();

	float getDeltaTime();
	void setDeltaTime(float _dt);
	float getTime();
	sf::Vector2f getViewSize();

	void drawRect(sf::VertexArray& va, b2Body* b, b2Fixture* f, const sf::Vector2f& dim, const sf::Color& c = sf::Color::Magenta);
	void spawnCreature(unsigned island_idx);
	std::unique_ptr<Creature> crossoveredCreature(std::unique_ptr<Creature> &parent_1, std::unique_ptr<Creature> &parent_2);
	sf::Vector2f getSpawnPos();
private:
	void stepIsland(unsigned idx, unsigned steps);
	void advanceIslandGeneration(unsigned idx, const sf::Vector2f& spawn_pos);
	// Deep-clones a creature's genome into a fresh body in dest_world (unlike
	// Creature::copy(), which always rebuilds in the source's own world) so a
	// creature can migrate between islands without crossing Box2D worlds.
	std::unique_ptr<Creature> cloneCreatureToWorld(Creature* src, b2World* dest_world);

	float m_accumulator = 0.0f;
	float m_dt = 1 / 125.0f;
	sf::RenderWindow m_window;
	sf::Vector2f m_view_size;
	float m_global_timer = 0.0f;
	unsigned m_gen_step = 0;
	bool m_first_launch = true;
	sf::Font m_font;

	sf::View view;
	b2Vec2 gravity;

	// The population is split into independent islands, each with its own
	// Box2D world and its own sub-population, so islands can be stepped and
	// bred fully in parallel across cores.
	std::vector<std::unique_ptr<b2World>> m_worlds;
	std::vector<std::vector<std::unique_ptr<Creature>>> m_islands;
	std::vector<std::unique_ptr<Creature>> m_island_candidates; // scratch: best creature per island this generation

	std::vector<b2Body*> groundBodies;
	std::vector<b2Fixture*> groundFixtures;
	sf::Vector2f groundDim;

	sf::Vector2f viewDimensions;

	float best_x = 0.0f;
	// Genetic Algorithm
	unsigned gen = 1;
	unsigned curr_island_id = 0;
	unsigned curr_creature_id = 0;

	bool j_last_frame = false;
	bool simulate_quick = false;

	bool cam_key_last_frame = false;
	bool show_best = false;

	// Requested flying rule; copied to Creature::allow_flying at the next generation boundary.
	bool m_allow_flying = true;
	bool f_last_frame = false;

	std::unique_ptr<Creature> best_creature = nullptr;
	unsigned m_best_world_idx = 0; // which island's world best_creature's bodies live in
	int best_c_gen = 0;
	int best_c_id = 0;
};

#endif // GAME_H