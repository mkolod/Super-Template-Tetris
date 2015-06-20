#pragma once

#include "block_generator.h"
#include "direction.h"
#include "input.h"
#include "grid.h"
#include "printer.h"
#include "random.h"
#include "serialize.h"
#include "playfield.h"
#include "buffer.h"

/**
    Seed for the random number generator.
*/
template <unsigned max>
using InitialRandom = LinearGenerator<uint32_t, 12345>;

/**
    Number of game steps to delay before a block is automatically placed.
    
    Movement resets the delay.
*/
static const size_t standardDelay = 1;

/**
    General state of the player
*/
enum class PlayerState : unsigned
{
    Alive,
    Dead
};

/**
    State of a snake game.
*/
template <
    PlayerState currentPlayerState,
    unsigned currentScore,
    size_t delay,
    typename currentPosition,
    typename currentBlock,
    typename currentWorld,
    typename currentBlockGenerator>
struct State
{
    static constexpr const PlayerState playerState = currentPlayerState;
    static constexpr const unsigned score = currentScore;
    
    using position = currentPosition;
    using block = currentBlock;
    
    using world = currentWorld;
    
    using random = currentBlockGenerator;
    using nextBlock = typename currentBlockGenerator::next::value;

    template <typename newPos>
    using set_position = State<playerState, score, delay, newPos, block, world, random>;
    
    template <typename newWorld>
    using set_world = State<playerState, score, delay, position, block, newWorld, random>;
    
    template <typename newRandom>
    using set_random = State<playerState, score, delay, position, block, world, newRandom>;
    
    template <typename newBlock>
    using set_block = State<playerState, score, delay, position, newBlock, world, random>;
    
    /**
    */
    using place_initial_piece = State<
        playerState,
        score,
        delay,
        Position<
            (currentWorld::width / 2) - (nextBlock::pieces::width) / 2,
            0>,
        nextBlock,
        world,
        typename random::next>;
};

/**
    The initial state of a snake game.
*/
using InitialState =
    typename State<
        PlayerState::Alive,
        0,
        0,
        Position<0, 0>,
        typename initialBlockGenerator::value,
        InitialWorld,
        initialBlockGenerator>::place_initial_piece;

/**
    Place the current piece in the world and generate a new piece.
*/
template <typename s>
using place_piece =
    typename s:: template set_world<
        buffer_draw_grid<
            typename s::position,
            typename s::block::pieces,
            typename s::world>
        >::place_initial_piece;

/**
    Hard drop the current piece.
*/
template <typename state>
struct HardDrop {
    template <typename s>
    struct Drop {
        using next = typename s::template set_position<typename s::position::template add<Position<0, 1>>>;
        
        struct con {
            using type = typename Drop<next>::type;
        };
        
        using type = branch_t<
            playfield_is_colliding<typename next::position, typename next::block::pieces, typename next::world>::value,
            identity<s>,
            con>;
    };
    
    using type = typename Drop<state>::type;
};

/**
    Attempt to move the block without checking for any collisions.
*/
template <Input input, typename state>
struct move_block;

template <typename state>
struct move_block<Input::Left, state> {
    using type = typename state::template set_position<typename state::position::template add<Position<-1, 0>>>;
};

template <typename state>
struct move_block<Input::Right, state> {
    using type = typename state::template set_position<typename state::position::template add<Position<1, 0>>>;
};

template <typename state>
struct move_block<Input::RRot, state> {
    using type = typename state::template set_block<typename state::block::rotateCw>;
};

template <typename state>
struct move_block<Input::LRot, state> {
    using type = typename state::template set_block<typename state::block::rotateCcw>;
};

/**
    Move the block in response to player input.
    
    This happens before gravity is applied.
*/
template <
    Input input,
    typename state>
struct move {
    using next = typename move_block<input, state>::type;
    
    using type = std::conditional_t<
        playfield_is_colliding<
            typename next::position,
            typename next::block::pieces,
            typename next::world>::value,
        state,
        next>;
};

/**
    Progress the game by one step by consuming one input.
*/
template <
    Input input,
    typename state>
struct step {
    /**
        Apply gravity to the current piece but keep it alive if it collides.
    */
    template <typename s>
    struct Down {
        using gnext = typename s::template set_position<typename s::position::template add<Position<0, 1>>>;
        
        using type = std::conditional_t<
            playfield_is_colliding<typename gnext::position, typename gnext::block::pieces, typename gnext::world>::value,
            s,
            gnext>;
    };

    using type = typename Down<typename move<input, state>::type>::type;
};

/**
    Hard drop the current piece.
*/
template <typename state>
struct step<Input::Up, state> {
    using type = place_piece<typename HardDrop<state>::type>;
};

/**
    For the case where we are dead, noop.
*/
template <
    Input input,
    unsigned score,
    size_t delay,
    typename position,
    typename block,
    typename world,
    typename blockGenerator>
struct step<input, State<PlayerState::Dead, score, delay, position, block, world, blockGenerator>> {
    using type = State<PlayerState::Dead, score, delay, position, block, world, blockGenerator>;
};

template <Input input, typename state>
using step_t = typename step<input, state>::type;


/*------------------------------------------------------------------------------
    Printer
*/
template <
    PlayerState playerState,
    unsigned score,
    size_t delay,
    typename position,
    typename block,
    typename world,
    typename blockGenerator>
struct Printer<State<playerState, score, delay, position, block, world, blockGenerator>>
{
    using self = State<playerState, score, delay, position, block, world, blockGenerator>;
    
    // Draw outline
    using initial_buffer = buffer_draw_rect_outline<
        Position<0, 0>,
        world::width + 2,
        world::height + 2,
        Pixel<'+', default_gfx>,
        empty_buffer<world::width + 2 + 10, world::height + 2>>;
    
    // Draw next block
    using next_block = buffer_draw_grid<
        Position<world::width + 2 + 2, 2>,
        typename self::nextBlock::pieces,
        initial_buffer>;
        
    // Draw death area
    using death_buffer = buffer_draw_rect<
        Position<1, 1>,
        world::width,
        deathZoneHeight,
        Pixel<'-', default_gfx>,
        next_block>;
    
    // draw playfield
    using play_buffer = buffer_draw_grid<
        Position<1, 1>,
        world,
        death_buffer>;

    // Draw current block
    using block_buffer = buffer_draw_grid<
        Position<1, 1>::add<position>,
        typename block::pieces,
        play_buffer>;

    using ghostState = typename HardDrop<self>::type;

    struct ToGhostPiece {
        template <typename x>
        using apply =
            std::conditional<std::is_same<x, empty_pixel>::value,
                empty_pixel,
                Pixel<'~', default_gfx>>;
    };

    using ghostPiece = fmap_t<ToGhostPiece, typename ghostState::block::pieces>;

    // Draw ghost
    using buffer = buffer_draw_grid<
        Position<1, 1>::add<typename ghostState::position>,
        ghostPiece,
        block_buffer>;

    static void Print(std::ostream& output)
    {
        output << "Score:" << score;
        output << " -- " << (playerState == PlayerState::Dead ? " You Are Dead " : "");
        output << "\n";
        Printer<buffer>::Print(output);
    }
};

/*------------------------------------------------------------------------------
    Serialize
*/
template <PlayerState state>
struct Serialize<SerializableValue<PlayerState, state>>
{
    static std::ostream& Write(std::ostream& output)
    {
        output << "PlayerState::";
        switch (state)
        {
        case PlayerState::Alive: output << "Alive"; break;
        case PlayerState::Dead: output << "Dead"; break;
        }
        return output;
    }
};

template <
    PlayerState playerState,
    unsigned score,
    size_t delay,
    typename position,
    typename block,
    typename world,
    typename blockGenerator>
struct Serialize<State<playerState, score, delay, position, block, world, blockGenerator>>
{
    static std::ostream& Write(std::ostream& output)
    {
        output << "State<";
        Join<',',
            SerializableValue<PlayerState, playerState>,
            SerializableValue<unsigned, score>,
            SerializableValue<size_t, delay>,
            block,
            world,
            blockGenerator>::Write(output);
        return output << ">";
    }
};
