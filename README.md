```
 _____  ____  __  __ _    _ _____     
|  __ \|___ \|  \/  | |  | | ____|    
| |__) | __) | \  / | |  | | |____  __
|  _  / |__ <| |\/| | |  | |___ \ \/ /
| | \ \ ___) | |  | | |__| |___) >  < 
|_|  \_\____/|_|  |_|\____/|____/_/\_\
 
```

# ChaosEngine ♟️

A deliberately chaotic UCI chess engine in a single C++ file.

It plays **legal** chess — castling, en passant, promotion, check detection, the
whole thing — but with a wild streak. A `Chaos` slider mixes pure-random moves
with a tiny material search, so you can dial it anywhere from "vaguely sensible"
to "throws pieces into the void for fun".

It will not beat Stockfish. That is not the point. The point is that it loses in
*interesting* ways.

## Build

Requires a C++17 compiler (`g++` or `clang++`).

```bash
g++ -O2 -std=c++17 -o chaos chaos.cpp
```

## Run

ChaosEngine speaks the [UCI protocol](https://en.wikipedia.org/wiki/Universal_Chess_Interface),
so it drops into any chess GUI (Cute Chess, Arena, BanksiaGUI) or you can poke it
directly in the terminal:

```bash
./chaos
```
```
uci
position startpos moves e2e4 e7e5
go
```

It will reply with `bestmove <move>`. Quit with `quit`.

## The Chaos option

The engine exposes one UCI option, `Chaos`, ranging from 0 to 100
(default 60):

| Value | Behaviour                                                        |
|-------|-----------------------------------------------------------------|
| `0`   | Always plays the best move from a shallow 2-ply search          |
| `60`  | Spicy — material-aware, but with random jitter so it varies     |
| `100` | Total madness — plays almost any legal move                     |

Set it in a GUI's engine settings, or over UCI:

```
setoption name Chaos value 100
```

## Play it against Stockfish

Using `cutechess-cli` (high chaos vs. a deliberately weakened Stockfish so it's
not a complete massacre):

```bash
cutechess-cli \
  -engine name=Chaos cmd=./chaos proto=uci option.Chaos=100 \
  -engine name=Stockfish cmd=stockfish proto=uci option.UCI_LimitStrength=true option.UCI_Elo=1350 \
  -each tc=10+0.1 \
  -games 10 \
  -pgnout chaos_vs_sf.pgn
```

Or pit two chaos levels against each other to see how much the tiny search
actually helps:

```bash
cutechess-cli \
  -engine name=Wild cmd=./chaos proto=uci option.Chaos=100 \
  -engine name=Tame cmd=./chaos proto=uci option.Chaos=10 \
  -each tc=5+0.1 -games 10
```

## How it works

A single file, roughly in this order:

- **Board model** — a 64-char array (`a8=0 … h1=63`, standard FEN ordering),
  plus side-to-move, castling rights and en-passant target.
- **Move generation** — pseudo-legal moves for every piece, then filtered for
  king safety (and castling-through-check).
- **Evaluation** — a tiny material count, nothing fancy.
- **Search** — a shallow negamax with alpha-beta, used only to keep low-chaos
  play from total self-destruction.
- **Move selection** — `pickMove()` rolls the dice: with probability `Chaos%`
  it yeets a random legal move, otherwise it runs the mini-search with a little
  centipawn jitter so it never plays the same game twice.

> **Note:** ChaosEngine ignores the clock — it moves instantly regardless of
> time control. That's fine for a random engine. Proper time management would be
> the first step toward actually making it stronger.

## Possible next steps

If you ever want it to *win* a game:

- Piece-square tables (so it knows knights belong in the centre)
- Deeper iterative-deepening search with real time management
- Quiescence search (stop hanging pieces in capture sequences)
- Transposition tables

## License

MIT — do whatever you like with it.
