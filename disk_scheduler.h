/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║         ADVANCED DISK SCHEDULING SIMULATOR                  ║
 * ║         disk_scheduler.h  —  Core Header                    ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║  Subject   : CSE-316 Operating Systems                      ║
 * ║  Project   : CA2 — Advanced Disk Scheduling Simulator       ║
 * ║  Language  : C (algorithm engine)                           ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  This header defines every data structure and function prototype
 *  used by the C engine. The design mirrors how real OS kernels
 *  represent disk I/O queues:
 *
 *    Linux block layer : struct request  (include/linux/blkdev.h)
 *    Windows I/O mgr   : IRP (I/O Request Packet)
 *
 *  We simplify for simulation but keep the same logical structure.
 */

#ifndef DISK_SCHEDULER_H
#define DISK_SCHEDULER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <time.h>

/* ══════════════════════════════════════════════════
 *  CONSTANTS
 * ══════════════════════════════════════════════════ */

#define MAX_REQUESTS      512
#define MAX_DISK_SIZE     100000
#define MAX_SEQ_LEN       (MAX_REQUESTS * 3)  /* extra room for SCAN boundary visits */
#define AGING_THRESHOLD   5                   /* boost priority after N passes        */
#define VERSION           "2.0"

/* Algorithm IDs */
typedef enum {
    ALGO_FCFS  = 0,
    ALGO_SSTF  = 1,
    ALGO_SCAN  = 2,
    ALGO_CSCAN = 3,
    ALGO_LOOK  = 4,   /* Bonus: LOOK algorithm (SCAN without going to disk ends) */
    ALGO_COUNT = 5
} AlgoID;

/* Scan direction */
typedef enum {
    DIR_LEFT  = 0,
    DIR_RIGHT = 1
} Direction;

/* ══════════════════════════════════════════════════
 *  DATA STRUCTURES
 * ══════════════════════════════════════════════════ */

/*
 * IORequest — one pending disk I/O request.
 * In a real OS this would also hold process ID, priority,
 * buffer pointer, completion callback, etc.
 */
typedef struct {
    int cylinder;       /* target cylinder number                  */
    int request_id;     /* original position in input queue (0-based) */
    int wait_count;     /* how many algorithm passes have skipped it   */
    int served;         /* 1 = already served in this run              */
} IORequest;

/*
 * StepInfo — metadata for one step of the simulation.
 * Stored so the dashboard can replay the animation step-by-step.
 */
typedef struct {
    int from_cylinder;   /* head position before this move   */
    int to_cylinder;     /* head position after this move    */
    int distance;        /* |to - from|                      */
    int req_id;          /* which request was served (-1 = boundary) */
    int starvation_flag; /* 1 if aging was triggered this step       */
} StepInfo;

/*
 * SeekResult — complete output from one algorithm run.
 * This is serialised to JSON and sent to the Python layer.
 */
typedef struct {
    /* Identity */
    AlgoID  algo_id;
    char    algo_name[16];

    /* Seek sequence (cylinder numbers visited in order) */
    int     seek_sequence[MAX_SEQ_LEN];
    int     seq_length;

    /* Per-step breakdown (for step-by-step animation) */
    StepInfo steps[MAX_SEQ_LEN];
    int      num_steps;

    /* Aggregate metrics */
    int     total_seek_distance;
    double  avg_seek_distance;   /* per request */
    double  max_seek_distance;   /* worst single jump */
    double  min_seek_distance;   /* best single jump  */
    double  variance;            /* variance of step distances */
    double  std_deviation;
    double  throughput;          /* requests / total_distance */
    int     starvation_count;    /* how many times aging fired */

    /* Config echo (for verification) */
    int     head_start;
    int     disk_size;
    int     num_requests;
} SeekResult;

/*
 * SimConfig — everything needed to run one simulation.
 */
typedef struct {
    IORequest  requests[MAX_REQUESTS];
    int        num_requests;
    int        head_position;
    int        disk_size;
    Direction  direction;
    int        enable_aging;   /* 1 = simulate request aging / starvation prevention */
} SimConfig;

/* ══════════════════════════════════════════════════
 *  FUNCTION PROTOTYPES
 * ══════════════════════════════════════════════════ */

/* Core algorithms */
SeekResult algo_fcfs (SimConfig *cfg);
SeekResult algo_sstf (SimConfig *cfg);
SeekResult algo_scan (SimConfig *cfg);
SeekResult algo_cscan(SimConfig *cfg);
SeekResult algo_look (SimConfig *cfg);  /* Bonus */

/* Batch */
void run_all(SimConfig *cfg, SeekResult out[ALGO_COUNT]);

/* Output */
void   print_result       (const SeekResult *r);
void   print_comparison   (const SeekResult results[], int count);
void   print_ascii_chart  (const SeekResult *r);
int    result_to_json     (const SeekResult *r, char *buf, int bufsz);
int    all_results_to_json(const SeekResult results[], int count, char *buf, int bufsz);

/* Utilities */
int    cmp_int            (const void *a, const void *b);
int    nearest_unserved   (const IORequest *reqs, int n, int head);
void   compute_statistics (SeekResult *r);
void   init_config        (SimConfig *cfg, int *cylinders, int n, int head, int disk_size, Direction dir);

#endif /* DISK_SCHEDULER_H */
