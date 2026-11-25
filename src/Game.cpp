#define _CRT_SECURE_NO_WARNINGS
#include "Game.h"
#include "utility.h"
#include <iostream>
#include <thread>
#include <mutex>

Game Game::m_instance;
const unsigned POPULATION = 1000;
// How often (in generations) islands exchange their best creature, to limit
// each island's independent population from settling into its own local optimum.
const unsigned MIGRATION_INTERVAL = 5;

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

	groundDim = sf::Vector2f(m_view_size.x, 1.0f);

	m_worlds.resize(num_islands);
	m_islands.resize(num_islands);
	m_island_candidates.resize(num_islands);
	groundBodies.resize(num_islands);
	groundFixtures.resize(num_islands);

	for (unsigned i = 0; i < num_islands; ++i) {
		m_worlds[i] = std::make_unique<b2World>(gravity);

		// Make the ground
		b2BodyDef groundBodyDef;
		groundBodyDef.position.Set(0.0f, m_view_size.y - groundDim.y*0.5f);

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
	m_window.create(sf::VideoMode(m_view_size.x, m_view_size.y), "KarsWorld", sf::Style::Default, window_settings);
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

		if (simulate_quick) update(); // Just simulate as fast as possible
		else {
			// Update
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::K)) elapsed *= 4.0; // Simulate 4x
			m_accumulator += elapsed;

			while (m_accumulator > m_dt) {
				m_accumulator -= m_dt;

				update();
			}

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

static inline float getFitness(float k) {
	return k*k*k;
}

int randomSelection(std::vector<std::unique_ptr<Creature>>& p) {
	float sum_fitness = 0;
	for (unsigned i = 0; i < p.size(); ++i) sum_fitness += getFitness(p[i]->fitness);

	float val = std::rand() % std::max(1, (int)sum_fitness);
	for (unsigned i = 0; i < p.size(); ++i) {
		val -= getFitness(p[i]->fitness);
		if (val <= 0) return i;
	}

	return p.size() - 1;
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

void Game::stepIsland(unsigned idx) {
	for (auto& c : m_islands[idx]) c->update(m_dt);
	m_worlds[idx]->Step(m_dt, 8, 3);
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
		//Tournament Selection
		std::vector<std::unique_ptr<Creature>> chosens;
		std::vector<int> chosen_indices;

		unsigned tournament_size = std::min<size_t>(4, creatures.size() - 1);
		unsigned selection_attempts = 0;
		unsigned max_weighted_attempts = (unsigned)creatures.size() * 20u + 100u;
		while (chosens.size() <= tournament_size) {
			// Fitness-weighted selection can degenerate to a single deterministic
			// index when total fitness is near zero (e.g. early generations, or
			// small islands). Fall back to a uniform pick so this always terminates.
			int electedIndex = (++selection_attempts <= max_weighted_attempts)
				? randomSelection(creatures)
				: random_int(0, (int)creatures.size() - 1);

			bool found = false;

			for (unsigned j = 0; j < chosen_indices.size(); j++) {
				if (chosen_indices[j] == electedIndex) {
					found = true;
					break;
				}
			}

			if (!found) {
				chosen_indices.push_back(electedIndex);
				chosens.push_back(creatures[electedIndex]->copy());
			}
		}

		std::sort(chosens.begin(), chosens.end(), [](const std::unique_ptr<Creature>& c1,
			const std::unique_ptr<Creature>& c2) { return c1->fitness > c2->fitness; });

		std::unique_ptr<Creature> cross_child = crossoveredCreature(chosens[0], chosens[1]);

		new_creatures.push_back(cross_child->mutatedCopy());

		new_creatures.back()->setPosition(spawn_pos);
	}

	// Make the new population, current population
	for (unsigned i = 0; i < creatures.size(); ++i)
		creatures[i] = std::move(new_creatures[i]);

	// Activate the whole new generation at once
	for (auto& c : creatures) c->setActive(true);
}

void Game::update() {
	m_global_timer += m_dt;

	best_creature->update(m_dt, false);

	// Step every island's physics and creatures in parallel across cores.
	{
		std::vector<std::thread> threads;
		threads.reserve(m_worlds.size());
		for (unsigned i = 0; i < m_worlds.size(); ++i)
			threads.emplace_back(&Game::stepIsland, this, i);
		for (auto& t : threads) t.join();
	}

	// Evaluate the whole population for 15s, then breed the next generation
	if (m_global_timer >= 15.0f) {
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

		// Pick the best creature across all islands
		unsigned best_island = 0;
		for (unsigned i = 1; i < m_island_candidates.size(); ++i)
			if (m_island_candidates[i]->fitness > m_island_candidates[best_island]->fitness) best_island = i;

		if (m_island_candidates[best_island]->fitness > best_creature->fitness) {
			best_c_gen = gen;
			best_c_id = best_island;
			best_creature = std::move(m_island_candidates[best_island]);
		}
		else { // Reset
			best_creature = best_creature->copy();
		}
		best_creature->setActive(true);
		best_creature->setPosition(spawn_pos);

		// Periodically exchange each island's best creature with its neighbor's,
		// so a converged island can be rescued by genes from elsewhere.
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

		curr_island_id = 0;
		curr_creature_id = 0;
		++gen;
	}
}

void Game::render() {
	m_window.clear(sf::Color(135, 206, 250));

	float curr_x = m_islands[curr_island_id][curr_creature_id]->fitness;
	float focus_x = show_best ? best_creature->getPosition().x : curr_x;
	best_x = focus_x;


	view.reset(sf::FloatRect(focus_x - viewDimensions.x*0.5f, m_view_size.y - viewDimensions.y, viewDimensions.x, viewDimensions.y));
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
	for (int i = 0; i <= 300; ++i) {
		line.setPosition(i, groundBodies[0]->GetPosition().y);
		m_window.draw(line);

		text.setString(std::to_string(i) + " m");
		text.setOrigin(text.getGlobalBounds().width*0.5f, text.getGlobalBounds().height*0.5f);
		text.setPosition(i - text.getCharacterSize()*0.01f, line.getPosition().y - line.getSize().y);

		text_back.setPosition(i, text.getGlobalBounds().top + text.getGlobalBounds().height*0.5f);
		m_window.draw(text_back);
		m_window.draw(text);
	}

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


	// Draw Creatures
	best_creature->render(m_window);
	m_islands[curr_island_id][curr_creature_id]->render(m_window);


	// Draw ground
	drawRect(va, groundBodies[0], groundFixtures[0], groundDim, sf::Color(0, 104, 10));

	m_window.draw(va);

	m_window.display();
}

std::unique_ptr<Creature> Game::crossoveredCreature(std::unique_ptr<Creature> &parent_1, std::unique_ptr<Creature> &parent_2) {
	
	std::unique_ptr<Creature> child = std::make_unique<Creature>();
	child->world = parent_1->world;
	child->heart_beat = parent_1->heart_beat;
	

	int randomCromosomeIndex = random_int(0, parent_1->nodes.size() - 1);

	for (int i = 0; i <= randomCromosomeIndex; i++) {
		child->nodes.push_back(parent_1->nodes[i]->copy());
	}

	for (int m = 0; m < parent_1->muscles.size(); m++) {
		if (parent_1->muscles[m]->a <= randomCromosomeIndex && parent_1->muscles[m]->b <= randomCromosomeIndex) {
			child->addMuscle(parent_1->muscles[m]->a, parent_1->muscles[m]->b);
		}
	}

	if (randomCromosomeIndex < parent_2->nodes.size()) {
		for (int i = randomCromosomeIndex; i<parent_2->nodes.size(); i++) {
			child->nodes.push_back(parent_2->nodes[i]->copy());
		}
		
		for (int m = 0; m < parent_2->muscles.size(); m++) {
			if (parent_2->muscles[m]->a >= randomCromosomeIndex && parent_2->muscles[m]->b >= randomCromosomeIndex) {
				child->addMuscle(parent_2->muscles[m]->a, parent_2->muscles[m]->b);
			}
		}
	}

	bool connectionMuscleFound = false;
	for (int m = 0; m < child->muscles.size(); m++) {
		for (int i = 0; i < child->nodes.size(); i++) {
			for (int j = 0; j < child->nodes.size(); j++) {
				if ((child->muscles[m]->a != i || child->muscles[m]->b != j) && i != j) {
					child->addMuscle(i, j);
					connectionMuscleFound = true;
					break;
				}
			}
			if (connectionMuscleFound) {
				break;
			}
		}
		if (connectionMuscleFound) {
			break;
		}
	}

	child->saveSnapshot(child->snapshot);
	child->checkLoneNodes();

	return child;
}


float Game::getDeltaTime() { return m_dt; }
void Game::setDeltaTime(float _dt) { m_dt = _dt; }
float Game::getTime() { return m_global_timer; }
sf::Vector2f Game::getViewSize() { return m_view_size; }