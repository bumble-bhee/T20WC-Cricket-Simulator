# T20 World Cup 2026 – Cricket Simulator

Assignment 1 | Operating Systems (CSC-204) | IIT Roorkee

---

## What is this?

This is a multi-threaded cricket simulator written in C++ for our OS course assignment. The idea is to model a T20 cricket match (India vs Australia) using OS concepts — threads, mutexes, semaphores, condition variables, scheduling algorithms, and deadlock detection — so that the match itself becomes a live demonstration of how an operating system manages concurrent processes.

The match is India's innings against Australia. Every entity on the field — batsmen, bowler, fielders — runs as a separate POSIX thread, and the pitch is the critical section that only one thread can "own" at a time.

---

## Scenario (from the assignment)

| Cricket Term       | OS Concept                  |
|--------------------|-----------------------------|
| Pitch              | Critical Section            |
| Batsman Thread     | Reader process              |
| Bowler Thread      | Writer process              |
| Fielder Threads    | Passive/sleeping threads    |
| Crease (2 batsmen) | Semaphore with capacity = 2 |
| Third Umpire       | Resource Scheduler          |
| Run-out situation  | Deadlock (circular wait)    |
| Umpire kills one   | OS kills a process          |

---

## Teams

India (Batting): Sanju Samson, Abhishek Sharma, Ishan Kishan, Suryakumar Yadav, Hardik Pandya, Tilak Verma, Shivam Dube, Axar Patel, Jasprit Bumrah, Varun Chakaravarthy, Arshdeep Singh

Australia (Bowling/Fielding): Marcus Stoinis, Xavier Bartlett, Josh Hazlewood, Nathan Ellis, Adam Zampa (bowlers) + Josh Inglis (WK) and rest of the fielding side.

---

## How to compile and run

You need g++ with C++17 support and pthreads. On any Linux machine:

```bash
g++ -std=c++17 -Wall -pthread t20_simulator.cpp -o t20sim
./t20sim
```

That's it. No external libraries needed. The program runs two full innings back-to-back (one FCFS, one SJF) and then prints the analysis at the end.

---

## What the simulator does

Once you run it, the terminal output shows a live ball-by-ball commentary with color coding:

- Green = boundaries (4s and 6s)
- Red = wickets
- Yellow = extras (wides and no-balls)
- White = regular balls

At the end of each innings, it prints:

1. **Scorecard** — runs, balls, strike rate for each batsman; economy for each bowler
2. **Gantt Chart** — shows which bowler was bowling and which batsman was facing for every delivery, with timestamps in milliseconds
3. **Wait Time Analysis** — compares how long middle-order batsmen had to wait before getting to bat under FCFS vs SJF

The simulator runs the same match twice using the same random seed so the comparison between FCFS and SJF is fair.

---

## OS concepts implemented

### Threads (pthreads)
- 1 Bowler thread — delivers the ball (writes to the Pitch buffer)
- 2 active Batsman threads — read the ball and update the score
- 10 Fielder threads — sleep until a BALL_HIT signal wakes them up
- 11 Batsman threads total are created at start; they wait in a queue until called to bat

### Mutexes
Used everywhere the shared state gets modified — `score_mtx` for the score, `over_mtx` for over/striker tracking, `print_mtx` so output doesn't get jumbled across threads, and a few more.

### Semaphore (`crease_sem`, capacity = 2)
Only 2 batsmen can be on the crease at a time. If somehow a third thread tries to enter, it blocks on `sem_wait()` until one of the two leaves.

### Condition Variables
- Fielders use `pthread_cond_wait` on `fielder_cv` — they only wake up when `BallInAir` is set to true by the batsman thread.
- The bowler waits on `pitchDone_cv` for the batsman to finish processing the ball before bowling the next one.
- New batsmen wait on `admit_cv` until the umpire (bowler thread) calls them in.

### Scheduling
Three scheduling ideas are combined:

**Round Robin (RR)** — Bowlers rotate every 6 balls (one over = one time quantum). After an over, the current bowler's stats are "context switched" out and the next bowler loads in.

**Shortest Job First (SJF)** — When a wicket falls, the next batsman to come in is picked based on estimated balls they'll face (tail-enders first). This is the SJF run.

**Priority Scheduling** — Once the match enters death overs (over 16 onwards), `Intensity` is set to 1 and Nathan Ellis (marked as `death_spec = true`) gets priority over other bowlers.

**FCFS** — The default natural batting order (openers first, tail-enders last).

### Deadlock Detection (Run-out scenario)
This was the trickiest part. When two batsmen are both running and could end up wanting the same crease (End 1 and End 2), we model it as a Resource Allocation Graph (RAG):

- Batsman A holds End 1, wants End 2
- Batsman B holds End 2, wants End 1
- That's a circular wait → deadlock

We run a DFS-based cycle detection on the RAG. If a cycle is found, the umpire "kills" one of the processes (non-striker is given run out). The thread is dismissed, a new batsman is dequeued, and the game continues.

Run-outs don't happen every ball, there's a 1-in-30 chance on a running delivery, just to keep things interesting without it being too frequent.

---

## Assumptions we made

- Only India's innings is simulated (one team bats). The target and Australia's chase are not modeled.
- Ball outcome probabilities are hardcoded per batsman role (Opener, Middle Order, Finisher, All-rounder, Tail-ender) and match phase (Powerplay: overs 1–6, Middle: 7–15, Death: 16–20). These weights were tuned to roughly match real T20 scoring patterns.
- A batsman who has faced 20+ balls and scored 18+ runs gets a slight wicket probability reduction (settled-in batsman effect).
- The "death over specialist" is Nathan Ellis (index 3 in the bowlers array). In real T20 WC 2026 cricket, this might be different — we just needed someone to demonstrate priority scheduling.
- Run-out is triggered randomly with 1/30 probability on running deliveries after over 1, to demonstrate the deadlock detection without it happening too often.
- The `estballs` values assigned to each batsman (e.g., Sanju Samson = 28, Arshdeep = 4) are rough estimates based on typical T20 batting order expectations, not real historical data.
- Thread sleep times (55ms per delivery, etc.) are only for simulation pacing — they don't represent real time.

---

## Output breakdown

**Ball notation:** `over.ball_number`, e.g., `3.4` means over 3, 4th legal delivery. Extras are shown as `3.x`.

**Gantt Chart columns:** Over | Ball | Bowler | Striker | Outcome | Start(ms) | End(ms) | Duration(ms)

**Wait Time Analysis:** Shows each batsman's name, role, estimated balls (priority for SJF), and actual wait in milliseconds before they got to bat. Middle-order batsmen are highlighted since those are the ones SJF is supposed to affect.

---

## File structure

```
.
├── t20_simulator.cpp    # All the code, single file
└── README.md
```

---

## Deliverables 

- [x] Code with ball-by-ball log output
- [x] Gantt chart (printed to terminal after each innings)
- [x] FCFS vs SJF wait time analysis

---

---

*CSC-204 Operating Systems | Assignment 1 | T20 WC Cricket Simulator*
