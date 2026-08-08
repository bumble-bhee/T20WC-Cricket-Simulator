// T20 World Cup 2026 – India vs Australia Simulator
// Compile: g++ -std=c++17 -Wall -pthread t20_simulator.cpp -o t20sim
// Run : ./t20sim

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <set>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <atomic>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

using namespace std;
using namespace std::chrono;

#define RST  "\033[0m"
#define BOLD "\033[1m"
#define RED  "\033[31m"
#define GRN  "\033[32m"
#define YEL  "\033[33m"
#define BLU  "\033[34m"
#define MAG  "\033[35m"
#define CYN  "\033[36m"
#define WHT  "\033[37m"

const int TOTAL_OVERS      = 20;
const int BALLS_PER_OVER   = 6;
const int NUM_FIELDERS     = 10;
const int NUM_BOWLERS      = 5;
const int NUM_BATSMEN      = 11;
const int DELIVERY_US      = 55000;
const int DEATH_OVER_START = 15;

enum BallOutcome { DOT=0,ONE=1,TWO=2,THREE=3,FOUR=4,SIX=6,WIDE=7,NOBALL=8,WICKET=9 };
enum Phase       { POWERPLAY=0, MIDDLE=1, DEATH=2 };
enum BatsmanRole { OPENER, MIDDLE_ORDER, FINISHER, ALLROUNDER, TAIL_ENDER };
enum Strategy    { FCFS, SJF_STRAT };

Phase get_phase(int over) {
    if(over < 6)                return POWERPLAY;
    if(over < DEATH_OVER_START) return MIDDLE;
    return DEATH;
}

const char* outcome_str(BallOutcome o) {
    switch(o) {
        case DOT:    return "Dot ball";
        case ONE:    return "1 run   ";
        case TWO:    return "2 runs  ";
        case THREE:  return "3 runs  ";
        case FOUR:   return "FOUR!   ";
        case SIX:    return "SIX!!   ";
        case WIDE:   return "Wide    ";
        case NOBALL: return "No-ball ";
        case WICKET: return "WICKET! ";
        default:     return "?       ";
    }
}

// Probability for balloutcome based on 5 roles ( opener, middle order, finisher, allrounder, tailender )
// three phases ( powerplay, middle orders, death overs ).
const int WT[5][3][9] = {
    {{340,270,100,20,145,65,28,10,27},{350,272,96,17,130,57,28,10,37},{300,265,94,16,150,80,28,10,46}},
    {{344,278,98,16,140,56,28,10,29},{362,278,96,15,127,50,28,10,34},{305,262,88,14,152,86,28,10,50}},
    {{308,258,86,14,160,94,28,10,43},{320,258,86,14,148,86,28,10,48},{284,252,82,12,162,108,28,10,58}},
    {{370,268,82,10,112,54,28,10,58},{368,272,82,10,108,52,28,10,67},{328,260,78,10,136,68,28,10,77}},
    {{424,255,62,5,68,18,28,10,130},{424,255,62,5,68,18,28,10,130},{400,255,62,5,72,20,28,10,148}}
};

BallOutcome roll_ball(BatsmanRole role, int over, int balls_faced, int runs_scored) {
    int w[9];
    memcpy(w, WT[(int)role][(int)get_phase(over)], sizeof w);
    if(balls_faced >= 20 && runs_scored >= 18) {
        int cut = (w[8]*22)/100;
        w[8] -= cut; w[0] += cut;
    }
    int total = 0; for(int x : w) total += x;
    int r = rand() % total, sum= 0;
    const BallOutcome out[] = {DOT,ONE,TWO,THREE,FOUR,SIX,WIDE,NOBALL,WICKET};
    for(int i = 0; i < 9; i++) { sum+= w[i]; if(r < sum) return out[i]; }
    return DOT;
}

struct Batsman {
    string name;
    int number, estballs, priority;
    BatsmanRole role;
    int runs=0, balls=0, fours=0, sixes=0;
    bool dismissed=false, on_crease=false, admitted=false, runout_victim=false;
    long wait_ms=0;
    int bat_order=-1;
    steady_clock::time_point arrive_tp, bat_tp;
};

struct Bowler {
    string name;
    int overs=0, total_balls=0, runs=0, wkts=0, balls_this_over=0;
    bool death_spec=false;
    int ctx_runs=0, ctx_wkts=0;
};

struct Fielder { string name; int id; bool is_wk=false; };

struct GanttEntry {
    string bowler, striker;
    int over, ball;
    long t0, t1;
    BallOutcome outcome;
};

const int END1=100, END2=101;
map<int,set<int>> rag;
pthread_mutex_t rag_mtx = PTHREAD_MUTEX_INITIALIZER;

void rag_add(int from, int to) {
    pthread_mutex_lock(&rag_mtx); rag[from].insert(to); pthread_mutex_unlock(&rag_mtx);
}
void rag_clear_node(int n) {
    pthread_mutex_lock(&rag_mtx);
    rag.erase(n);
    for(auto& [k,s] : rag) s.erase(n);
    pthread_mutex_unlock(&rag_mtx);
}
bool dfs(int n, set<int>& vis, set<int>& stk, map<int,set<int>>& g) {
    vis.insert(n); stk.insert(n);
    for(int nb : g[n]) {
        if(!vis.count(nb) && dfs(nb,vis,stk,g)) return true;
        if(stk.count(nb)) return true;
    }
    stk.erase(n); return false;
}
bool has_cycle() {
    pthread_mutex_lock(&rag_mtx); auto g = rag; pthread_mutex_unlock(&rag_mtx);
    set<int> vis, stk;
    for(auto& [n,_] : g) if(!vis.count(n) && dfs(n,vis,stk,g)) return true;
    return false;
}

int Score=0, Wickets=0, CurOver=0, BallsInOver=0, OversCompleted=0;
atomic<bool> InningsOver(false);
atomic<int>  Intensity(0), bat_order_counter(0);

struct { BallOutcome outcome=DOT; bool ready=false,consumed=true; int runs_scored=0; } Pitch;
bool BallInAir=false, BoundaryHit=false;

pthread_mutex_t admit_mtx      = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  admit_cv       = PTHREAD_COND_INITIALIZER;
pthread_mutex_t score_mtx      = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t over_mtx       = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_mtx      = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t print_mtx      = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gantt_mtx      = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t fielder_mtx    = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t pitchReady_mtx  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t pitchDone_mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t boundary_mtx   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t creaseReady_mtx  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  creaseReady_cv   = PTHREAD_COND_INITIALIZER;
bool            creaseReadyFlag = false;
pthread_cond_t  fielder_cv     = PTHREAD_COND_INITIALIZER;
pthread_cond_t  pitchReady_cv   = PTHREAD_COND_INITIALIZER;
pthread_cond_t  pitchDone_cv  = PTHREAD_COND_INITIALIZER;
sem_t crease_sem;

Batsman India[NUM_BATSMEN] = {
    {"Sanju Samson",        1, 28,  1, OPENER      },
    {"Abhishek Sharma",     2, 24,  2, OPENER      },
    {"Ishan Kishan",        3, 36,  3, MIDDLE_ORDER},
    {"Suryakumar Yadav",    4, 20,  4, MIDDLE_ORDER},
    {"Hardik Pandya",       5, 18,  5, FINISHER    },
    {"Tilak Verma",         6, 16,  6, FINISHER    },
    {"Shivam Dube",         7, 12,  7, ALLROUNDER  },
    {"Axar Patel",          8, 10,  8, ALLROUNDER  },
    {"Jasprit Bumrah",      9,  6,  9, TAIL_ENDER  },
    {"Varun Chakarvarthy", 10,  5, 10, TAIL_ENDER  },
    {"Arshdeep Singh",     11,  4, 11, TAIL_ENDER  }
};

Bowler AusBowlers[NUM_BOWLERS] = {
    {"Marcus Stoinis"},
    {"Xavier Bartlett"},
    {"Josh Hazlewood"},
    {"Nathan Ellis"},
    {"Adam Zampa"}
};

Fielder AusFielders[NUM_FIELDERS] = {
    {"Josh Inglis (WK)", 0, true}, {"Tim David",       1},
    {"Steve Smith",      2},       {"Mitchell Marsh",  3},
    {"Glenn Maxwell",    4},       {"Travis Head",     5},
    {"Cameron Green",    6},       {"Cooper Connolly", 7},
    {"Matt Renshaw",     8},       {"Marcus Stoinis",  9}
};

int striker_idx=0, nonstriker_idx=1, currentBowler=0;
Strategy g_strategy = FCFS;
queue<int> fcfs_q;
priority_queue<pair<int,int>,vector<pair<int,int>>,greater<pair<int,int>>> sjf_q;
vector<GanttEntry> gantt_log;
steady_clock::time_point innings_start;

long now_ms() { return duration_cast<milliseconds>(steady_clock::now()-innings_start).count(); }

void tlog(const string& s) {
    pthread_mutex_lock(&print_mtx); cout<<s<<"\n"; pthread_mutex_unlock(&print_mtx);
}

void wake_all() {
    pthread_mutex_lock(&fielder_mtx);    BallInAir=true; pthread_cond_broadcast(&fielder_cv);        pthread_mutex_unlock(&fielder_mtx);
    pthread_mutex_lock(&pitchReady_mtx);  pthread_cond_broadcast(&pitchReady_cv);                       pthread_mutex_unlock(&pitchReady_mtx);
    pthread_mutex_lock(&pitchDone_mtx); Pitch.consumed=true; pthread_cond_broadcast(&pitchDone_cv); pthread_mutex_unlock(&pitchDone_mtx);
    pthread_mutex_lock(&admit_mtx);      pthread_cond_broadcast(&admit_cv);                           pthread_mutex_unlock(&admit_mtx);
    pthread_mutex_lock(&creaseReady_mtx); pthread_cond_broadcast(&creaseReady_cv);                      pthread_mutex_unlock(&creaseReady_mtx);
}

void wait_for_new_batsman() {
    pthread_mutex_lock(&creaseReady_mtx);
    while(!creaseReadyFlag && !InningsOver.load())
        pthread_cond_wait(&creaseReady_cv, &creaseReady_mtx);
    creaseReadyFlag = false;
    pthread_mutex_unlock(&creaseReady_mtx);
}

void signal_crease_ready() {
    pthread_mutex_lock(&creaseReady_mtx);
    creaseReadyFlag = true;
    pthread_cond_signal(&creaseReady_cv);
    pthread_mutex_unlock(&creaseReady_mtx);
}

int dequeue_next() {
    pthread_mutex_lock(&queue_mtx);
    int idx = -1;
    if(g_strategy==SJF_STRAT) { if(!sjf_q.empty()) { idx=sjf_q.top().second; sjf_q.pop(); } }
    else                       { if(!fcfs_q.empty()) { idx=fcfs_q.front(); fcfs_q.pop(); } }
    pthread_mutex_unlock(&queue_mtx);
    return idx;
}

void admit_batsman(int idx) {
    if(idx<0||idx>=NUM_BATSMEN) return;
    pthread_mutex_lock(&admit_mtx);
    India[idx].admitted = true;
    pthread_cond_broadcast(&admit_cv);
    pthread_mutex_unlock(&admit_mtx);
}

// Run out handler
// It checks the resource allocation graph to find any deadlock
// if deadlock is found the process is killed i.e. the non-striker batsman is given out
bool handle_runout() {
    int A=striker_idx, B=nonstriker_idx;
    rag_add(END1,A); rag_add(A,END2); rag_add(END2,B); rag_add(B,END1);

    // (1) [run-out] line
    tlog(string(YEL)+"[run-out] "+India[A].name+" & "+India[B].name+" both running – checking RAG..."+RST);

    bool admitted_new = false;
    if(has_cycle()) {
        pthread_mutex_lock(&score_mtx);
        India[B].dismissed=true; India[B].runout_victim=true; Wickets++;
        pthread_mutex_unlock(&score_mtx);

        // (2) [deadlock] line
        tlog(string(RED)+BOLD+"[deadlock] "+India[B].name+" is run out.  Score: "
             +to_string(Score)+"/"+to_string(Wickets)+RST);

        pthread_mutex_lock(&pitchReady_mtx); pthread_cond_broadcast(&pitchReady_cv); pthread_mutex_unlock(&pitchReady_mtx);
        India[B].on_crease = false;
        rag_clear_node(B);
        sem_post(&crease_sem);

        if(Wickets < 10) {
            int ni = dequeue_next();
            if(ni >= 0) {
                pthread_mutex_lock(&over_mtx); nonstriker_idx=ni; pthread_mutex_unlock(&over_mtx);
                admit_batsman(ni);
                admitted_new = true;
            }
        } else { InningsOver.store(true); wake_all(); }
    } else {
        tlog(string(GRN)+"  [safe] No cycle detected – both batsmen made it ."+RST);
    }
    rag_clear_node(A); rag_clear_node(B); rag_clear_node(END1); rag_clear_node(END2);
    if(!InningsOver.load()) { rag_add(END1,striker_idx); rag_add(END2,nonstriker_idx); }
    return admitted_new;
}

void* fielder_thread(void* a) {
    int idx = *(int*)a; delete (int*)a;
    Fielder& f = AusFielders[idx];
    while(!InningsOver.load()) {
        pthread_mutex_lock(&fielder_mtx);
        while(!BallInAir && !InningsOver.load()) pthread_cond_wait(&fielder_cv,&fielder_mtx);
        if(!InningsOver.load() && BallInAir) {
            BallInAir = false; pthread_mutex_unlock(&fielder_mtx);
            pthread_mutex_lock(&boundary_mtx); bool ib=BoundaryHit; BoundaryHit=false; pthread_mutex_unlock(&boundary_mtx);
            usleep(10000);
            if(!ib) tlog(string("      ")+WHT+(f.is_wk?"[WK] ":"[F]  ")+f.name+" fields."+RST);
        } else { pthread_mutex_unlock(&fielder_mtx); }
        usleep(4000);
    }
    return nullptr;
}

void* batsman_thread(void* a) {
    int idx = *(int*)a; delete (int*)a;
    Batsman& bat = India[idx];
    bat.arrive_tp = steady_clock::now();

    pthread_mutex_lock(&admit_mtx);
    while(!bat.admitted && !InningsOver.load()) pthread_cond_wait(&admit_cv,&admit_mtx);
    pthread_mutex_unlock(&admit_mtx);
    if(InningsOver.load()) return nullptr;

    bat.bat_tp    = steady_clock::now();
    bat.wait_ms   = duration_cast<milliseconds>(bat.bat_tp - bat.arrive_tp).count();
    bat.bat_order = bat_order_counter.fetch_add(1);

    sem_wait(&crease_sem);
    bat.on_crease = true;
    rag_add((idx==striker_idx)?END1:END2, idx);
    signal_crease_ready();

    while(!InningsOver.load() && !bat.dismissed) {
        pthread_mutex_lock(&pitchReady_mtx);
        while(!Pitch.ready && !InningsOver.load() && !bat.dismissed)
            pthread_cond_wait(&pitchReady_cv,&pitchReady_mtx);
        pthread_mutex_unlock(&pitchReady_mtx);
        if(InningsOver.load()||bat.dismissed) break;
        if(idx != striker_idx) { usleep(3000); continue; }

        BallOutcome outcome = Pitch.outcome;
        bool legal = (outcome!=WIDE && outcome!=NOBALL);
        int runs_add = 0;

        pthread_mutex_lock(&score_mtx);
        switch(outcome) {
            case DOT:    if(legal) bat.balls++;                                                           break;
            case ONE: case TWO: case THREE:
                         runs_add=(int)outcome; Score+=runs_add; bat.runs+=runs_add; bat.balls++;          break;
            case FOUR:   Score+=4; bat.runs+=4; bat.balls++; bat.fours++; runs_add=4;                    break;
            case SIX:    Score+=6; bat.runs+=6; bat.balls++; bat.sixes++; runs_add=6;                    break;
            case WIDE: case NOBALL: Score+=1; runs_add=1;                                                 break;
            case WICKET: bat.balls++; bat.dismissed=true; Wickets++;                                      break;
        }
        pthread_mutex_unlock(&score_mtx);

        if(!bat.dismissed && (outcome==ONE||outcome==TWO||outcome==THREE||outcome==DOT||outcome==WIDE||outcome==NOBALL)) {
            pthread_mutex_lock(&fielder_mtx); BallInAir=true; pthread_cond_broadcast(&fielder_cv); pthread_mutex_unlock(&fielder_mtx);
        }

        pthread_mutex_lock(&pitchDone_mtx);
        Pitch.runs_scored=runs_add; Pitch.consumed=true; Pitch.ready=false;
        pthread_cond_signal(&pitchDone_cv);
        pthread_mutex_unlock(&pitchDone_mtx);
        if(bat.dismissed) break;
        usleep(2000);
    }

    if(!bat.runout_victim) {
        bat.on_crease = false;
        rag_clear_node(idx);
        sem_post(&crease_sem);
    }
    return nullptr;
}

// Round Robin scheduling for bowlers
void rr_context_switch(int dOver) {
    pthread_mutex_lock(&over_mtx);
    Bowler& cur = AusBowlers[currentBowler];
    cur.ctx_runs=cur.runs; cur.ctx_wkts=cur.wkts;
    cur.overs++;

    string old_name = cur.name;
    int tries=0;
    do { currentBowler=(currentBowler+1)%NUM_BOWLERS; tries++; }
    while(AusBowlers[currentBowler].total_balls >= 24 && tries < NUM_BOWLERS);

    if(dOver+1 >= DEATH_OVER_START) {
        Intensity.store(1);
        for(int i=0;i<NUM_BOWLERS;i++) {
            if(AusBowlers[i].death_spec && AusBowlers[i].total_balls < 24) {
                currentBowler=i;
                tlog(string(MAG)+BOLD+"[priority] "+AusBowlers[i].name+" elected for death overs."+RST);
                break;
            }
        }
    }
    AusBowlers[currentBowler].balls_this_over=0;
    pthread_mutex_unlock(&over_mtx);

    ostringstream ss;
    tlog(ss.str());
}

void* bowler_thread(void*) {
    for(int over=0; over<TOTAL_OVERS; over++) {
        if(InningsOver.load()) break;

        pthread_mutex_lock(&over_mtx);
        CurOver=over; BallsInOver=0;
        if(over >= DEATH_OVER_START) Intensity.store(1);
        pthread_mutex_unlock(&over_mtx);

        Bowler& bw = AusBowlers[currentBowler];
        ostringstream hdr;
        hdr<<BOLD<<BLU<<"\nOver "<<(over+1)<<" | "<<bw.name
           <<" | Score: "<<Score<<"/"<<Wickets
           <<(Intensity?" | Death Overs":"")<<"\n"<<string(45,'-')<<RST;
        tlog(hdr.str());

        int legal=0;
        while(legal < BALLS_PER_OVER) {
            if(InningsOver.load() || Wickets>=10) break;

            long t0 = now_ms();
            string gantt_striker = India[striker_idx].name;
            BallOutcome outcome  = roll_ball(India[striker_idx].role, over,
                                             India[striker_idx].balls, India[striker_idx].runs);

            pthread_mutex_lock(&pitchDone_mtx);
            Pitch.outcome=outcome; Pitch.ready=true; Pitch.consumed=false; Pitch.runs_scored=0;
            pthread_mutex_unlock(&pitchDone_mtx);
            pthread_mutex_lock(&pitchReady_mtx); pthread_cond_broadcast(&pitchReady_cv); pthread_mutex_unlock(&pitchReady_mtx);

            pthread_mutex_lock(&pitchDone_mtx);
            while(!Pitch.consumed && !InningsOver.load()) pthread_cond_wait(&pitchDone_cv,&pitchDone_mtx);
            int runs_scored=Pitch.runs_scored;
            pthread_mutex_unlock(&pitchDone_mtx);
            if(InningsOver.load()) break;

            bool is_legal=(outcome!=WIDE && outcome!=NOBALL);
            if(is_legal) {
                legal++; BallsInOver++; bw.balls_this_over++;
                bw.total_balls++;
            }

            pthread_mutex_lock(&score_mtx);
            if(outcome==WICKET) bw.wkts++;
            else { int r=(outcome==FOUR)?4:(outcome==SIX)?6:(outcome==WIDE||outcome==NOBALL)?1:(int)outcome; bw.runs+=r; }
            pthread_mutex_unlock(&score_mtx);

            string col=(outcome==FOUR||outcome==SIX)?GRN:(outcome==WICKET)?RED:(outcome==WIDE||outcome==NOBALL)?YEL:WHT;
            string bid = to_string(over)+"."+(is_legal?to_string(legal):"x");
            ostringstream bl;
            bl<<col<<setw(5)<<left<<bid<<"  "<<setw(20)<<left<<gantt_striker
              <<setw(9)<<left<<outcome_str(outcome)<<"  "<<Score<<"/"<<Wickets<<RST;
            tlog(bl.str());

            if(outcome==WICKET) {
                tlog(string("  ")+RED+"[out] "+gantt_striker+" dismissed.  Score: "
                     +to_string(Score)+"/"+to_string(Wickets)+RST);

                if(Wickets<10 && !InningsOver.load()) {
                    int ni=dequeue_next();
                    if(ni>=0) {
                        pthread_mutex_lock(&over_mtx); striker_idx=ni; pthread_mutex_unlock(&over_mtx);
                        admit_batsman(ni);
                        if(!InningsOver.load()) wait_for_new_batsman();
                        tlog(string("  ")+GRN+"[crease] "+India[striker_idx].name
                             +" walks in. (waited "+to_string(India[striker_idx].wait_ms)+" ms)"+RST);
                    } else { InningsOver.store(true); wake_all(); }
                } else if(Wickets>=10) { InningsOver.store(true); wake_all(); }

            } else {
                bool running=(outcome==ONE||outcome==TWO||outcome==THREE);
                bool runout_admitted = false;
                if(running && over>0 && !InningsOver.load() && rand()%30==0) {
                    runout_admitted = handle_runout();
                }
                if(InningsOver.load()) break;
                if(runout_admitted && !InningsOver.load()) {
                    wait_for_new_batsman();
                    tlog(string("  ")+GRN+"[crease] "+India[nonstriker_idx].name
                         +" walks in. (waited "+to_string(India[nonstriker_idx].wait_ms)+" ms)"+RST);
                    if(is_legal && (runs_scored%2)==1 && !India[striker_idx].dismissed) {
                        pthread_mutex_lock(&over_mtx); swap(striker_idx,nonstriker_idx); pthread_mutex_unlock(&over_mtx);
                    }
                } else {
                    if(is_legal && (runs_scored%2)==1 && !India[striker_idx].dismissed) {
                        pthread_mutex_lock(&over_mtx); swap(striker_idx,nonstriker_idx); pthread_mutex_unlock(&over_mtx);
                    }
                }
            }

            usleep(DELIVERY_US);
            long t1=now_ms();
            pthread_mutex_lock(&gantt_mtx);
            gantt_log.push_back({bw.name,gantt_striker,over+1,legal,t0,t1,outcome});
            pthread_mutex_unlock(&gantt_mtx);
        }
        if(InningsOver.load() || Wickets>=10) break;

        pthread_mutex_lock(&over_mtx);
        swap(striker_idx,nonstriker_idx);
        OversCompleted = over+1;
        pthread_mutex_unlock(&over_mtx);
        if(over < TOTAL_OVERS-1) rr_context_switch(over);
    }

    pthread_mutex_lock(&over_mtx);
    AusBowlers[currentBowler].overs++;
    pthread_mutex_unlock(&over_mtx);

    InningsOver.store(true);
    wake_all();
    return nullptr;
}

void print_scorecard() {
    vector<Batsman*> played;
    for(auto& b : India) if(b.bat_order >= 0) played.push_back(&b);
    sort(played.begin(), played.end(), [](Batsman* a, Batsman* b){ return a->bat_order < b->bat_order; });

    cout<<BOLD<<GRN<<"\nIndia Innings Scorecard\n"<<string(52,'-')<<"\n"<<RST;
    cout<<BOLD<<left<<setw(22)<<"Batsman"<<setw(5)<<"R"<<setw(5)<<"B"<<setw(5)<<"4s"<<setw(5)<<"6s"<<right<<setw(8)<<"SR\n"<<RST;
    cout<<string(50,'-')<<"\n";
    for(auto* b : played) {
        double sr = b->balls > 0 ? 100.0*b->runs/b->balls : 0.0;
        string sfx = b->dismissed ? "" : " *";
        cout<<left<<setw(22)<<(b->name+sfx).substr(0,21)
            <<setw(5)<<b->runs<<setw(5)<<b->balls<<setw(5)<<b->fours<<setw(5)<<b->sixes
            <<right<<fixed<<setprecision(1)<<setw(8)<<sr<<"\n";
    }
    int total_legal = 0;
    for(auto& bw : AusBowlers) total_legal += bw.total_balls;
    string overs_disp = to_string(total_legal/6) + (total_legal%6 ? "."+to_string(total_legal%6) : "");
    cout<<string(50,'-')<<"\n"<<BOLD<<"Total: "<<Score<<"/"<<Wickets<<"  ("<<overs_disp<<" overs)\n"<<RST;

    cout<<BOLD<<BLU<<"\nAustralia Bowling\n"<<string(48,'-')<<"\n"<<RST;
    cout<<BOLD<<left<<setw(20)<<"Bowler"<<setw(7)<<"O"<<setw(5)<<"R"<<setw(5)<<"W"<<right<<setw(9)<<"Economy\n"<<RST;
    cout<<string(46,'-')<<"\n";
    for(auto& bw : AusBowlers) {
        if(!bw.total_balls) continue;
        int tb = bw.total_balls;
        string o_str = to_string(tb/6) + (tb%6 ? "."+to_string(tb%6) : "");
        double economy = (double)bw.runs * 6.0 / tb;
        cout<<left<<setw(20)<<bw.name.substr(0,19)<<setw(7)<<o_str
            <<setw(5)<<bw.runs<<setw(5)<<bw.wkts
            <<right<<fixed<<setprecision(2)<<setw(9)<<economy<<"\n";
    }
    cout<<string(46,'-')<<"\n";
}

void print_gantt() {
    cout<<BOLD<<CYN<<"\nGantt Chart – Delivery Timeline\n"
        <<"(time in ms from innings start)\n"
        <<string(82,'-')<<"\n"<<RST;
    cout<<BOLD<<left<<setw(5)<<"Over"<<setw(5)<<"Ball"<<setw(18)<<"Bowler"
        <<setw(20)<<"Striker"<<setw(10)<<"Outcome"<<right<<setw(8)<<"Start"<<setw(8)<<"End"<<setw(8)<<"Dur\n"<<RST;
    cout<<string(82,'-')<<"\n";
    int prev=-1;
    for(auto& g : gantt_log) {
        if(g.over != prev) { if(prev!=-1) cout<<"\n"; prev=g.over; }
        string col=(g.outcome==FOUR||g.outcome==SIX)?GRN:(g.outcome==WICKET)?RED:(g.outcome==WIDE||g.outcome==NOBALL)?YEL:"";
        cout<<col<<left<<setw(5)<<g.over<<setw(5)<<g.ball
            <<setw(18)<<g.bowler.substr(0,17)<<setw(20)<<g.striker.substr(0,19)
            <<setw(10)<<outcome_str(g.outcome)
            <<right<<setw(8)<<g.t0<<setw(8)<<g.t1<<setw(8)<<(g.t1-g.t0)<<RST<<"\n";
    }
    cout<<string(82,'-')<<"\n";
}

struct WaitResult { string name; BatsmanRole role; int estballs; long wait_ms; };

string role_str(BatsmanRole r) {
    switch(r) {
        case OPENER:       return "Opener";
        case MIDDLE_ORDER: return "Middle";
        case FINISHER:     return "Finisher";
        case ALLROUNDER:   return "AllRounder";
        case TAIL_ENDER:   return "Tail";
        default:           return "?";
    }
}

vector<WaitResult> collect_wait_results() {
    vector<pair<int,Batsman*>> tmp;
    for(auto& b : India) if(b.bat_order >= 0) tmp.push_back({b.bat_order,&b});
    sort(tmp.begin(),tmp.end());
    vector<WaitResult> out;
    for(auto& [_,b] : tmp) out.push_back({b->name,b->role,b->estballs,b->wait_ms});
    return out;
}

void print_analysis(const vector<WaitResult>& fcfs_r, const vector<WaitResult>& sjf_r) {
    cout<<BOLD<<MAG
        <<"\n Scheduling Analysis - Wait Time Comparison: FCFS vs SJF \n"<<RST;

    auto print_table = [](const string& label, const vector<WaitResult>& rows) -> pair<long,long> {
        cout<<BOLD<<"\n"<<label<<"\n"<<RST;
        cout<<left<<setw(22)<<"Batsman"<<setw(13)<<"Role"<<setw(11)<<"Est.Balls"<<"Wait(ms)\n";
        cout<<string(60,'-')<<"\n";
        long total=0,mo_total=0; int mo_cnt=0;
        for(auto& r : rows) {
            cout<<left<<setw(22)<<r.name.substr(0,21)<<setw(13)<<role_str(r.role)
                <<setw(11)<<r.estballs<<r.wait_ms<<" ms"
                <<(r.role==MIDDLE_ORDER?"  <-- middle order":"")<<"\n";
            total+=r.wait_ms;
            if(r.role==MIDDLE_ORDER){mo_total+=r.wait_ms; mo_cnt++;}
        }
        long aa = rows.empty()?0:total/(long)rows.size();
        long am = mo_cnt?mo_total/mo_cnt:0;
        cout<<string(60,'-')<<"\n"<<BOLD;
        cout<<"  Avg wait (all batsmen)  : "<<aa<<" ms\n";
        cout<<"  Avg wait (middle order) : "<<am<<" ms\n"<<RST;
        return {aa,am};
    };

    auto [fa,fm] = print_table("[FCFS] Natural Batting Order", fcfs_r);
    auto [sa,sm] = print_table("[SJF]  Shortest Job First",    sjf_r);

    long da=fa-sa, dm=fm-sm;
    if(0){
        cout<<da<<dm;
    }
}

void reset_state(unsigned seed) {
    srand(seed);
    Score=0; Wickets=0; CurOver=0; BallsInOver=0; OversCompleted=0;
    InningsOver.store(false); Intensity.store(0); bat_order_counter.store(0);
    Pitch.outcome=DOT; Pitch.ready=false; Pitch.consumed=true; Pitch.runs_scored=0;
    BallInAir=false; BoundaryHit=false;
    striker_idx=0; nonstriker_idx=1; currentBowler=0;
    creaseReadyFlag=false;
    rag.clear();

    for(int i=0;i<NUM_BATSMEN;i++) {
        India[i].runs=India[i].balls=India[i].fours=India[i].sixes=0;
        India[i].dismissed=India[i].on_crease=India[i].admitted=India[i].runout_victim=false;
        India[i].wait_ms=0; India[i].bat_order=-1;
        India[i].arrive_tp=India[i].bat_tp=steady_clock::time_point{};
    }

    for(int i=0;i<NUM_BOWLERS;i++) {
        AusBowlers[i].overs=AusBowlers[i].total_balls=AusBowlers[i].runs=0;
        AusBowlers[i].wkts=AusBowlers[i].balls_this_over=0;
        AusBowlers[i].ctx_runs=AusBowlers[i].ctx_wkts=0;
        AusBowlers[i].death_spec = (i==3);
    }

    while(!fcfs_q.empty()) fcfs_q.pop();
    while(!sjf_q.empty())  sjf_q.pop();
    for(int i=2;i<NUM_BATSMEN;i++) {
        fcfs_q.push(i);
        sjf_q.push({India[i].estballs, i});
    }
    gantt_log.clear();
    innings_start = steady_clock::now();
}

void run_innings(const string& label) {
    cout<<BOLD<<GRN
        <<"\n"
        <<" T20 World Cup 2026 – India vs Australia\n"
        <<" Venue: Wankhede Stadium, Mumbai  |  India Innings\n"
        <<" Strategy: "<<left<<setw(47)<<label<<"\n\n"<<RST;

    pthread_t f_th[NUM_FIELDERS];
    for(int i=0;i<NUM_FIELDERS;i++) { int* a=new int(i); pthread_create(&f_th[i],nullptr,fielder_thread,a); }

    pthread_t bat_th[NUM_BATSMEN];
    for(int i=0;i<NUM_BATSMEN;i++) { int* a=new int(i); pthread_create(&bat_th[i],nullptr,batsman_thread,a); }
    usleep(20000);

    admit_batsman(0); admit_batsman(1);
    usleep(15000);

    pthread_t bowl_th;
    pthread_create(&bowl_th,nullptr,bowler_thread,nullptr);
    pthread_join(bowl_th,nullptr);

    InningsOver.store(true);
    wake_all();
    for(int i=0;i<NUM_FIELDERS;i++) pthread_join(f_th[i],nullptr);
    for(int i=0;i<NUM_BATSMEN;i++)  pthread_join(bat_th[i],nullptr);
}

int main() {
    unsigned seed = (unsigned)time(nullptr);

    if(sem_init(&crease_sem,0,2)!=0) { cerr<<"sem_init failed\n"; return 1; }

    g_strategy = FCFS;
    reset_state(seed);
    run_innings("FCFS – Natural Batting Order");
    print_scorecard();
    print_gantt();
    vector<WaitResult> fcfs_results = collect_wait_results();

    sem_destroy(&crease_sem);
    if(sem_init(&crease_sem,0,2)!=0) { cerr<<"sem_init failed (run 2)\n"; return 1; }

    g_strategy = SJF_STRAT;
    reset_state(seed);
    run_innings("SJF – Shortest Job First");
    print_scorecard();
    print_gantt();
    vector<WaitResult> sjf_results = collect_wait_results();

    print_analysis(fcfs_results, sjf_results);

    sem_destroy(&crease_sem);
    pthread_mutex_destroy(&score_mtx);     pthread_mutex_destroy(&over_mtx);
    pthread_mutex_destroy(&queue_mtx);     pthread_mutex_destroy(&print_mtx);
    pthread_mutex_destroy(&gantt_mtx);     pthread_mutex_destroy(&fielder_mtx);
    pthread_mutex_destroy(&pitchReady_mtx); pthread_mutex_destroy(&pitchDone_mtx);
    pthread_mutex_destroy(&rag_mtx);       pthread_mutex_destroy(&boundary_mtx);
    pthread_mutex_destroy(&admit_mtx);     pthread_mutex_destroy(&creaseReady_mtx);
    pthread_cond_destroy(&fielder_cv);     pthread_cond_destroy(&pitchReady_cv);
    pthread_cond_destroy(&pitchDone_cv);  pthread_cond_destroy(&admit_cv);
    pthread_cond_destroy(&creaseReady_cv);
    return 0;
}
