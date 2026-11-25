# Evolution

A genetic-algorithm walking-creature simulator, built with [Box2D](https://box2d.org/) for physics
and [SFML](https://www.sfml-dev.org/) for rendering. Soft-body creatures made of nodes and muscles
evolve over generations to walk as far as possible.

## Building

Dependencies: CMake 3.10+, a C++14 compiler, SFML 2, and box2d.

```bash
# Debian/Ubuntu
sudo apt install libsfml-dev libbox2d-dev cmake g++

mkdir -p build && cd build
cmake ..
make
./Evolution
```

## Controls

| Key | Action |
|-----|--------|
| `J` | Toggle fast simulation (skips rendering, runs physics as fast as possible) |
| `K` | Hold to simulate at 4x speed |
| `H` | Toggle camera focus between the current test creature and the overall best |
| `Esc` | Close the window |

## How it works

Each creature is a small ragdoll of `Node`s (circular bodies) connected by `Muscle`s
(`b2DistanceJoint`s that oscillate between a short and long length). A creature's fitness is the
furthest horizontal distance it reaches during a fixed evaluation window.

### Islands

The population is split into independent **islands**, one per available CPU core
(`std::thread::hardware_concurrency()`). Each island owns its own `b2World`, ground body, and
sub-population, so islands are stepped and bred fully in parallel — this is what lets a large
population train much faster than a single sequential loop.

Every generation, each island:
1. Records its best creature.
2. Runs tournament selection + crossover + mutation on its own sub-population to produce the next
   generation.

Because islands evolve independently, every `MIGRATION_INTERVAL` generations (see `Game.cpp`) each
island receives a clone of a neighboring island's best creature, so a population that's converged
on a weak strategy has a chance to be rescued by genes from elsewhere.

### Genetic algorithm

- **Selection**: fitness-proportionate tournament selection (fitness weighted by `fitness^3` to
  favor stronger performers), picking distinct individuals per tournament.
- **Crossover**: a single-point crossover over a creature's nodes/muscles between two tournament
  winners.
- **Mutation**: small chances per generation to add/remove a node or muscle, plus continuous
  mutation of muscle lengths, timing, strength, and heartbeat.
- **Elitism**: each island's top creatures are carried into the next generation unmodified.

The best creature ever seen across all islands is tracked separately and always kept active and
rendered alongside whichever creature the camera is currently focused on.
