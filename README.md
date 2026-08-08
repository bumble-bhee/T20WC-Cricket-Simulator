# T20 World Cup Cricket Simulator

A multithreaded C++ simulation of a T20 cricket innings designed to demonstrate core Operating Systems concepts such as POSIX threads, mutexes, semaphores, condition variables, CPU scheduling and deadlock detection.

The simulator models an India vs Australia T20 innings where batsmen, bowlers and fielders are represented using concurrent threads. The same innings is executed using two scheduling strategies — FCFS and Shortest Job First (SJF) — and their waiting times are compared.

## Features

- Multithreaded cricket simulation using POSIX threads
- FCFS scheduling
- Shortest Job First (SJF) scheduling
- Mutex-based synchronization
- POSIX semaphores
- Condition variables
- Resource Allocation Graph (RAG)
- Circular-wait / deadlock detection
- Simulated context switching
- Ball-by-ball match commentary
- Batsman and bowler statistics
- Gantt-chart-style execution log
- FCFS vs SJF waiting-time analysis

## Operating Systems Concepts

### 1. POSIX Threads

The simulator creates separate threads for batsmen, bowlers and fielders using POSIX threads (`pthread`).

### 2. Synchronization

Shared match state is accessed by multiple concurrent threads. Mutexes and condition variables are used to coordinate access and execution between threads.

### 3. Semaphores

A POSIX semaphore models access to the batting crease, allowing a limited number of batsmen to occupy the resource simultaneously.

### 4. CPU Scheduling

Two scheduling strategies are simulated:

- **FCFS (First Come First Serve):** batsmen are selected according to their natural batting order.
- **SJF (Shortest Job First):** the next batsman is selected using the estimated number of balls they are expected to face.

The simulator records waiting times and compares the two strategies.

### 5. Deadlock Detection

A Resource Allocation Graph is used to represent resource dependencies during a simulated run-out situation. The graph is analyzed for cycles to demonstrate circular wait and deadlock detection.

## System Architecture

```text
                    T20 Cricket Simulator
                            |
             +--------------+--------------+
             |              |              |
          Batsmen         Bowler        Fielders
          Threads         Thread        Threads
             |              |              |
             +--------------+--------------+
                            |
                     Shared Match State
                            |
          +-----------------+-----------------+
          |                 |                 |
        Mutexes        Condition Variables   Semaphore
          |                 |                 |
          +-----------------+-----------------+
                            |
                    Scheduling Layer
                       /          \
                     FCFS         SJF
                       \          /
                        \        /
                       Analysis