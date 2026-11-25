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
| `F` | Toggle flying (applies from the next generation). With flying off, only distance covered while touching the ground counts |
| `Esc` | Close the window |

## How it works

Each creature is a small ragdoll of `Node`s (circular bodies) connected by `Muscle`s
(`b2DistanceJoint`s that oscillate between a short and long length). A creature's fitness is its
horizontal position at the end of a fixed 15-second evaluation window, so it has to keep moving
rather than just lunging forward once.

Creatures can learn to launch themselves into the air, which is allowed by default. With flying
turned off (`F`), fitness only adds up forward progress made while at least one node touches the
ground, so evolution favours walking.

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

- **Selection**: tournament selection. Each parent is the fittest of 3 creatures drawn at random.
- **Crossover**: the child keeps the fitter parent's body. Where both parents have the same node or
  muscle, each of its genes (friction, muscle lengths, timing, strength, and the heartbeat) comes
  from either parent at random.
- **Mutation**: each continuous gene has a 25% chance of being nudged by a normally distributed
  amount. There is also a small chance per child of adding or removing a node or muscle.
- **Elitism**: each island's top 3 creatures are carried into the next generation unmodified.

The best creature ever seen across all islands is tracked separately and always kept active and
rendered alongside whichever creature the camera is currently focused on.
