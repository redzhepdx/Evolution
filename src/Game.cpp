#define _CRT_SECURE_NO_WARNINGS
#include "Game.h"
#include "utility.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <thread>
#include <mutex>

Game Game::m_instance;
const unsigned POPULATION = 1000;
// How often (in generations) islands exchange their best creature, to limit
// each island's independent population from settling into its own local optimum.
const unsigned MIGRATION_INTERVAL = 5;
const float GENERATION_TIME = 15.0f;
// Ground extent (metres) behind and ahead of the spawn point at x = 0.
const float GROUND_BEHIND = 640.0f;
const float GROUND_AHEAD = 10000.0f;
// Steps each island runs per thread dispatch in fast-sim mode (~1 simulated second),
// so the window still polls events regularly.
const unsigned QUICK_BATCH_STEPS = 125;

static std::mutex g_cout_mutex;

void Game::init(float _dt) {
	setDeltaTime(_dt);
	m_view_size = sf::Vector2f(1280, 600);
	m_font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
	viewDimensions.y = 15.0f;
	viewDimensions.x = viewDimensions.y*m_view_size.x / m_view_size.y;

	view.reset(sf::FloatRect(-viewDimensions.x*0.5f, m_view_size.y - viewDimensions.y, viewDimensions.x, viewDimensions.y));
	view.setViewport(sf::FloatRect(0, 0, 1, 1));

	gravity = b2Vec2(0.0f, 9.81);

	// Split the population into independent islands (one Box2D world each)
	// so they can be stepped and bred fully in parallel across cores.
	unsigned num_islands = std::max(1u, std::thread::hardware_concurrency());
	num_islands = std::min(num_islands, POPULATION);

	groundDim = sf::Vector2f(GROUND_BEHIND + GROUND_AHEAD, 1.0f);

	m_worlds.resize(num_islands);
	m_islands.resize(num_islands);
	m_island_candidates.resize(num_islands);
	groundBodies.resize(num_islands);
	groundFixtures.resize(num_islands);

	for (unsigned i = 0; i < num_islands; ++i) {
		m_worlds[i] = std::make_unique<b2World>(gravity);

		// Make the ground
		b2BodyDef groundBodyDef;
		groundBodyDef.position.Set((GROUND_AHEAD - GROUND_BEHIND)*0.5f, m_view_size.y - groundDim.y*0.5f);

		groundBodies[i] = m_worlds[i]->CreateBody(&groundBodyDef);

		// Ground fixture
		b2PolygonShape groundBox;
		groundBox.SetAsBox(groundDim.x*0.5f, groundDim.y*0.5f);
		groundFixtures[i] = groundBodies[i]->CreateFixture(&groundBox, 0.0f);
		groundFixtures[i]->SetFriction(1.0);
	}

	// Distribute the population round-robin across islands
	for (unsigned i = 0; i < POPULATION; ++i) spawnCreature(i % num_islands);

	curr_island_id = 0;
	curr_creature_id = 0;
	for (auto& island : m_islands)
		for (auto& c : island) c->setActive(true);

	best_creature = m_islands[0][0]->copy();
	best_creature->setActive(true);
}

void Game::drawRect(sf::VertexArray& va, b2Body* b, b2Fixture* f, const sf::Vector2f& dim, const sf::Color& c) {
	sf::Vertex v;
	v.color = c;
	b2PolygonShape* polygonShape = (b2PolygonShape*)f->GetShape();

	for (int i = 0; i < polygonShape->m_count; ++i) {
		const b2Vec2& b2v = b->GetWorldPoint(polygonShape->m_vertices[i]);
		v.position.x = b2v.x;
		v.position.y = b2v.y;
		va.append(v);
	}
}

void Game::restart() {
	m_first_launch = false;
}

void Game::run() {
	sf::ContextSettings window_settings;
	window_settings.antialiasingLevel = 4;
	m_window.create(sf::VideoMode(m_view_size.x, m_view_size.y), "EvolutionarySprint", sf::Style::Default, window_settings);
	sf::Clock clock;

	while (m_window.isOpen()) {
		double elapsed = clock.restart().asSeconds();

		// Toggle fast simulation
		bool j_now = sf::Keyboard::isKeyPressed(sf::Keyboard::J);
		if (j_now && !j_last_frame) simulate_quick = !simulate_quick;
		j_last_frame = j_now;

		// Focus camera on the best one
		bool cam_key_now = sf::Keyboard::isKeyPressed(sf::Keyboard::H);
		if (cam_key_now && !cam_key_last_frame) show_best = !show_best;
		cam_key_last_frame = cam_key_now;

		// Toggle whether distance covered in the air counts towards fitness
		bool f_now = sf::Keyboard::isKeyPressed(sf::Keyboard::F);
		if (f_now && !f_last_frame) m_allow_flying = !m_allow_flying;
		f_last_frame = f_now;

		// Check events
		sf::Event event;
		while (m_window.pollEvent(event)) {
			if (event.type == sf::Event::Closed) {
				m_window.close();
			}
			if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
				m_window.close();
			}
		}

		if (simulate_quick) update(QUICK_BATCH_STEPS); // Just simulate as fast as possible
		else {
			// Update
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::K)) elapsed *= 4.0; // Simulate 4x
			m_accumulator += elapsed;

			unsigned pending = (unsigned)(m_accumulator / m_dt);
			m_accumulator -= pending * m_dt;
			while (pending > 0) pending -= update(pending);

			// Render
			render();
		}
	}
}
sf::Vector2f Game::getSpawnPos() { return sf::Vector2f(0, m_view_size.y - viewDimensions.y*0.2); }
void Game::spawnCreature(unsigned island_idx) {
	auto& island = m_islands[island_idx];
	island.push_back(std::make_unique<Creature>());
	island.back()->init(m_worlds[island_idx].get(), sf::Vector2f(0, 0));
	island.back()->setPosition(getSpawnPos());
}

const int TOURNAMENT_SIZE = 3;

// The fittest of k uniformly drawn creatures.
static int tournamentSelect(const std::vector<std::unique_ptr<Creature>>& p, int k) {
	int best = random_int(0, (int)p.size() - 1);
	for (int i = 1; i < k; ++i) {
		int c = random_int(0, (int)p.size() - 1);
		if (p[c]->fitness > p[best]->fitness) best = c;
	}
	return best;
}

std::unique_ptr<Creature> Game::cloneCreatureToWorld(Creature* src, b2World* dest_world) {
	auto dst = std::make_unique<Creature>();
	dst->world = dest_world;
	dst->heart_beat = src->heart_beat;

	for (auto& n : src->nodes) {
		auto new_node = std::make_unique<Node>();
		new_node->init(dest_world, n->getPosition(), n->fixture_def.friction);
		dst->nodes.push_back(std::move(new_node));
	}

	for (auto& m : src->muscles) {
		auto new_muscle = std::make_unique<Muscle>();
		new_muscle->init(dest_world, dst->nodes, m->a, m->b,
			m->short_len, m->long_len, m->extend_time, m->contract_time, m->strength);
		dst->muscles.push_back(std::move(new_muscle));
	}

	dst->setActive(false);
	dst->saveSnapshot(dst->snapshot);
	return dst;
}

void Game::stepIsland(unsigned idx, unsigned steps) {
	// best_creature's bodies live in one island's world, so only that island's thread may touch it.
	bool owns_best = (idx == m_best_world_idx);
	for (unsigned s = 0; s < steps; ++s) {
		if (owns_best) best_creature->update(m_dt, false);
		for (auto& c : m_islands[idx]) c->update(m_dt);
		m_worlds[idx]->Step(m_dt, 8, 3);
	}
}

void Game::advanceIslandGeneration(unsigned idx, const sf::Vector2f& spawn_pos) {
	auto& creatures = m_islands[idx];

	// Find this island's best creature before breeding reshuffles it
	unsigned local_best_idx = 0;
	for (unsigned i = 1; i < creatures.size(); ++i)
		if (creatures[i]->fitness > creatures[local_best_idx]->fitness) local_best_idx = i;
	m_island_candidates[idx] = creatures[local_best_idx]->copy();

	// Create this island's next generation
	std::sort(creatures.begin(), creatures.end(), [](const std::unique_ptr<Creature>& c1,
														 const std::unique_ptr<Creature>& c2)
														{ return c1->fitness > c2->fitness; });

	{
		std::lock_guard<std::mutex> lock(g_cout_mutex);
		std::cout << "> Island#" << idx << " G#" << gen << "\n";
		int print_count = std::min(2, (int)creatures.size());
		for (int i = 0; i < print_count; ++i) std::cout << creatures[i]->fitness << "\n";
	}

	std::vector<std::unique_ptr<Creature>> new_creatures;

	//Save best children, it will succeed as at least last population
	int keep_count = std::min<int>(3, (int)creatures.size());
	for (int i = 0; i < keep_count; i++) {
		new_creatures.push_back(creatures[i]->copy());
		new_creatures.back()->setPosition(spawn_pos);
	}

	for (int i = 0; i < (int)creatures.size() - keep_count; ++i) {
		int p1 = tournamentSelect(creatures, TOURNAMENT_SIZE);
		int p2 = tournamentSelect(creatures, TOURNAMENT_SIZE);
		for (int tries = 0; p2 == p1 && tries < 10; ++tries) p2 = tournamentSelect(creatures, TOURNAMENT_SIZE);
		if (creatures[p2]->fitness > creatures[p1]->fitness) std::swap(p1, p2);

		// Breed from the parents' snapshots (their spawn pose), not their post-run pose.
		std::unique_ptr<Creature> cross_child = crossoveredCreature(creatures[p1]->snapshot, creatures[p2]->snapshot);

		new_creatures.push_back(cross_child->mutatedCopy());

		new_creatures.back()->setPosition(spawn_pos);
	}

	// Make the new population, current population
	for (unsigned i = 0; i < creatures.size(); ++i)
		creatures[i] = std::move(new_creatures[i]);

	// Activate the whole new generation at once. Fitness restarts from zero because
	// ground-only fitness accumulates, and elites would otherwise keep last generation's score.
	for (auto& c : creatures) {
		c->fitness = 0.0f;
		c->setActive(true);
	}
}

unsigned Game::update(unsigned max_steps) {
	const unsigned gen_steps = (unsigned)std::lround(GENERATION_TIME / m_dt);
	unsigned steps = std::min(max_steps, gen_steps - m_gen_step);
	if (steps == 0) return 0;

	// Islands don't interact within a generation, so each thread runs its whole
	// batch of steps independently and we only sync once per batch.
	{
		std::vector<std::thread> threads;
		threads.reserve(m_worlds.size());
		for (unsigned i = 0; i < m_worlds.size(); ++i)
			threads.emplace_back(&Game::stepIsland, this, i, steps);
		for (auto& t : threads) t.join();
	}

	m_gen_step += steps;
	m_global_timer = m_gen_step * m_dt;

	// Evaluate the whole population for GENERATION_TIME, then breed the next generation
	if (m_gen_step >= gen_steps) {
		m_gen_step = 0;
		m_global_timer = 0.0f;

		sf::Vector2f spawn_pos = getSpawnPos();

		for (auto& island : m_islands)
			for (auto& c : island) c->setActive(false);

		// Each island breeds its own next generation independently, so this
		// also runs fully in parallel.
		{
			std::vector<std::thread> threads;
			threads.reserve(m_islands.size());
			for (unsigned i = 0; i < m_islands.size(); ++i)
				threads.emplace_back(&Game::advanceIslandGeneration, this, i, spawn_pos);
			for (auto& t : threads) t.join();
		}

		// Periodically exchange each island's best creature with its neighbor's,
		// so a converged island can be rescued by genes from elsewhere.
		// Must run before picking the overall best, which moves a candidate out.
		if (m_islands.size() > 1 && gen % MIGRATION_INTERVAL == 0) {
			std::vector<std::unique_ptr<Creature>> migrants(m_islands.size());
			for (unsigned i = 0; i < m_islands.size(); ++i) {
				unsigned src_island = (i + (unsigned)m_islands.size() - 1) % (unsigned)m_islands.size();
				migrants[i] = cloneCreatureToWorld(m_island_candidates[src_island].get(), m_worlds[i].get());
			}
			for (unsigned i = 0; i < m_islands.size(); ++i) {
				auto& creatures = m_islands[i];
				if (creatures.size() > 3) { // don't clobber the kept elites
					migrants[i]->setPosition(spawn_pos);
					migrants[i]->setActive(true);
					creatures.back() = std::move(migrants[i]);
				}
			}
		}

		// Pick the best creature across all islands
		unsigned best_island = 0;
		for (unsigned i = 1; i < m_island_candidates.size(); ++i)
			if (m_island_candidates[i]->fitness > m_island_candidates[best_island]->fitness) best_island = i;

		if (m_island_candidates[best_island]->fitness > best_creature->fitness) {
			best_c_gen = gen;
			best_c_id = best_island;
			best_creature = std::move(m_island_candidates[best_island]);
			m_best_world_idx = best_island;
		}
		else { // Reset
			best_creature = best_creature->copy();
		}
		best_creature->setActive(true);
		best_creature->setPosition(spawn_pos);

		curr_island_id = 0;
		curr_creature_id = 0;
		++gen;
		Creature::allow_flying = m_allow_flying;
	}
	return steps;
}

void Game::render() {
	m_window.clear(sf::Color(135, 206, 250));

	const Creature* focus = show_best ? best_creature.get() : m_islands[curr_island_id][curr_creature_id].get();
	float focus_x = focus->getPosition().x;
	best_x = focus_x;

	// Keep the ground in shot, but rise with the creature once it flies into the top 40% of the view.
	float ground_y = groundBodies[0]->GetPosition().y;
	float default_top = m_view_size.y - viewDimensions.y;
	float view_top = std::min(default_top, focus->getPosition().y - viewDimensions.y*0.4f);

	view.reset(sf::FloatRect(focus_x - viewDimensions.x*0.5f, view_top, viewDimensions.x, viewDimensions.y));
	m_window.setView(view);

	sf::VertexArray va(sf::Quads);

	// Draw background lines
	sf::Text text;
	text.setFont(m_font);
	text.setCharacterSize(12);
	text.setFillColor(sf::Color::Black);
	float scale = viewDimensions.y / m_view_size.y;
	text.setScale(scale, scale);

	sf::RectangleShape text_back(sf::Vector2f(0.7, 0.3));
	text_back.setOrigin(text_back.getSize()*0.5f);

	sf::RectangleShape line(sf::Vector2f(0.025, 4.0));
	line.setOrigin(line.getSize().x*0.5f, line.getSize().y);
	int first_marker = std::max(0, (int)std::floor(focus_x - viewDimensions.x*0.5f));
	int last_marker = std::min((int)GROUND_AHEAD, (int)std::ceil(focus_x + viewDimensions.x*0.5f));
	for (int i = first_marker; i <= last_marker; ++i) {
		line.setPosition(i, ground_y);
		m_window.draw(line);

		text.setString(std::to_string(i) + " m");
		text.setOrigin(text.getGlobalBounds().width*0.5f, text.getGlobalBounds().height*0.5f);
		text.setPosition(i - text.getCharacterSize()*0.01f, line.getPosition().y - line.getSize().y);

		text_back.setPosition(i, text.getGlobalBounds().top + text.getGlobalBounds().height*0.5f);
		m_window.draw(text_back);
		m_window.draw(text);
	}

	// The HUD below stacks upwards from this point, fixed relative to the view
	// (where the distance labels sit when the camera is at ground level).
	text.setPosition(focus_x, view_top + (ground_y - line.getSize().y - default_top));

	// Show the best
	text.setString(setPrecision(best_x, 2) + " m");
	text.setOrigin(text.getGlobalBounds().width*0.5f, text.getGlobalBounds().height*0.5f);
	text.setPosition(best_x - text.getCharacterSize()*0.01f, text.getPosition().y - 1);
	text_back.setPosition(best_x, text.getGlobalBounds().top + text.getGlobalBounds().height*0.5f);
	text_back.setSize(sf::Vector2f(text_back.getSize().x*2.0, text_back.getSize().y));
	text_back.setOrigin(text_back.getSize()*0.5f);
	m_window.draw(text_back);
	m_window.draw(text);

	// Show the timer
	text.setString("Time: " + setPrecision(m_global_timer, 2) + " sec");
	text.setOrigin(0, 0);
	text.setPosition(best_x, text.getPosition().y - 1);
	text_back.setPosition(best_x, text.getGlobalBounds().top + text.getGlobalBounds().height*0.5f);
	text_back.setSize(sf::Vector2f(text_back.getSize().x*1.4, text_back.getSize().y));
	text_back.setOrigin(0.2, text_back.getSize().y*0.5f);
	m_window.draw(text_back);
	m_window.draw(text);

	// Show the overall best
	text.setString("Current Gen #" + std::to_string(gen) +
		"  Island #" + std::to_string(curr_island_id + 1) +
		"  ID #" + std::to_string(curr_creature_id + 1) +
		"                   Best Creature:  Gen #" + std::to_string(best_c_gen) +
		"  Island #" + std::to_string(best_c_id + 1) +
		"    Dist: " + setPrecision(best_creature->fitness, 2) + " m");
	text.setOrigin(text.getGlobalBounds().width*0.5f, text.getGlobalBounds().height*0.5f);
	text.setPosition(best_x - text.getCharacterSize()*0.01f - 1, text.getPosition().y - 1);
	text_back.setPosition(best_x - 1, text.getGlobalBounds().top + text.getGlobalBounds().height*0.5f);
	text_back.setSize(sf::Vector2f(text_back.getSize().x*6.0, text_back.getSize().y));
	text_back.setOrigin(0.2, text_back.getSize().y*0.5f);
	m_window.draw(text_back);
	m_window.draw(text);

	// Show the flying rule
	std::string flying = m_allow_flying ? "on" : "off";
	if (m_allow_flying != Creature::allow_flying) flying += " from next generation";
	text.setString("Flying (F): " + flying);
	text.setOrigin(0, 0);
	text.setPosition(best_x, text.getPosition().y - 1);
	sf::FloatRect bounds = text.getGlobalBounds();
	text_back.setOrigin(0, 0);
	text_back.setSize(sf::Vector2f(bounds.width + 0.4f, text_back.getSize().y));
	text_back.setPosition(bounds.left - 0.2f, bounds.top + bounds.height*0.5f - text_back.getSize().y*0.5f);
	m_window.draw(text_back);
	m_window.draw(text);


	// Draw Creatures
	best_creature->render(m_window);
	m_islands[curr_island_id][curr_creature_id]->render(m_window);


	// Draw ground
	drawRect(va, groundBodies[0], groundFixtures[0], groundDim, sf::Color(0, 104, 10));

	m_window.draw(va);

	m_window.display();
}

// The child keeps parent_1's body (the fitter parent) and takes each gene from either
// parent at random where parent_2 has a matching node or muscle (same node indices).
std::unique_ptr<Creature> Game::crossoveredCreature(std::unique_ptr<Creature> &parent_1, std::unique_ptr<Creature> &parent_2) {
	auto coin = []() { return random_int(0, 1) == 1; };

	std::unique_ptr<Creature> child = std::make_unique<Creature>();
	child->world = parent_1->world;
	child->heart_beat = coin() ? parent_2->heart_beat : parent_1->heart_beat;

	for (unsigned i = 0; i < parent_1->nodes.size(); ++i) {
		Node* n = parent_1->nodes[i].get();
		float friction = n->fixture_def.friction;
		if (i < parent_2->nodes.size() && coin()) friction = parent_2->nodes[i]->fixture_def.friction;

		auto node = std::make_unique<Node>();
		node->init(child->world, n->getPosition(), friction);
		child->nodes.push_back(std::move(node));
	}

	for (auto& m1 : parent_1->muscles) {
		Muscle* genes = m1.get();
		if (coin()) {
			for (auto& m2 : parent_2->muscles) {
				if ((m2->a == m1->a && m2->b == m1->b) || (m2->a == m1->b && m2->b == m1->a)) {
					genes = m2.get();
					break;
				}
			}
		}

		auto muscle = std::make_unique<Muscle>();
		muscle->init(child->world, child->nodes, m1->a, m1->b,
			genes->short_len, genes->long_len, genes->extend_time, genes->contract_time, genes->strength);
		child->muscles.push_back(std::move(muscle));
	}

	child->setActive(false);
	child->saveSnapshot(child->snapshot);
	return child;
}


float Game::getDeltaTime() { return m_dt; }
void Game::setDeltaTime(float _dt) { m_dt = _dt; }
float Game::getTime() { return m_global_timer; }
sf::Vector2f Game::getViewSize() { return m_view_size; }